/*
 * Copyright (C) 2026 Slava Monich <slava@monich.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#include "YubiKeyOpQueue.h"

#include "YubiKeyAuth.h"
#include "YubiKeyConstants.h"
#include "YubiKeyIo.h"
#include "YubiKeyUtil.h"

#include "HarbourParentSignalQueueObject.h"
#include "HarbourDebug.h"
#include "HarbourUtil.h"

#include <QtCore/QPointer>
#include <QtCore/QQueue>
#include <QtCore/QScopedPointer>
#include <QtCore/QVector>

#define Q_STATES(s) \
    s(QueueIdle) \
    s(QueuePrepare) \
    s(QueueBlocked) \
    s(QueueActive)

#define OP_STATES(s) \
    s(OpQueued) \
    s(OpActive) \
    s(OpCancelled) \
    s(OpFinished) \
    s(OpFailed)

// s(SignalName,signalName,Suffix)
#define QUEUED_SIGNALS(s) \
    s(OpQueueState,opQueueState,Changed) \
    s(OpIds,opIds,Changed) \
    s(YubiKeyId,yubiKeyId,Changed) \
    s(YubiKeySerial,yubiKeySerial,Changed) \
    s(YubiKeyFwVersion,yubiKeyFwVersion,Changed) \
    s(YubiKeyAuthAccess,yubiKeyAuthAccess,Changed) \
    s(YubiKeyConnected,yubiKeyConnected,) \
    s(YubiKeyValidationFailed,yubiKeyValidationFailed,) \
    s(InvalidYubiKeyConnected,invalidYubiKeyConnected,) \
    s(RestrictedYubiKeyConnected,restrictedYubiKeyConnected,)

#if HARBOUR_DEBUG
QDebug
operator<<(
    QDebug aDebug,
    YubiKeyOpQueue::State aState)
{
    switch (aState) {
    #define STATE_(s) case YubiKeyOpQueue::s: return (aDebug << #s);
    Q_STATES(STATE_)
    #undef STATE_
    }
    return aDebug << (int)aState;
}

QDebug
operator<<(
    QDebug aDebug,
    YubiKeyOp::OpState aState)
{
    switch (aState) {
    #define STATE_(s) case YubiKeyOp::s: return (aDebug << #s);
    OP_STATES(STATE_)
    #undef STATE_
    }
    return aDebug << (int)aState;
}
#endif // HARBOUR_DEBUG

typedef QScopedPointer<YubiKeyIoTx, HarbourUtil::ScopedPointerDeleteLater>
    TxScopedPointer;

// ==========================================================================
// YubiKeyOpQueue::Entry (internal representation of YubiKeyOp)
// ==========================================================================

class YubiKeyOpQueue::Entry :
    public YubiKeyOp
{
    Q_OBJECT

public:
    Entry(Private*, const YubiKeyIo::APDU&, Flags, Priority, int, OpData*);
    ~Entry();

    Private* owner() const;
    void setOpState(OpState);
    const char* name() const;
    bool start();
    void resetTx();
    bool hasQueuedSignals() const;
    void emitQueuedSignals();

    // YubiKeyOp
    OpData* opData() const Q_DECL_OVERRIDE;
    OpState opState() const Q_DECL_OVERRIDE;
    int opId() const Q_DECL_OVERRIDE;
    void opCancel() Q_DECL_OVERRIDE;
    bool opIsDone() const Q_DECL_OVERRIDE;

private:
    bool setTx(YubiKeyIoTx*);
    void sendRemaining(uint);

private Q_SLOTS:
    void onTxCancelled();
    void onTxFailed();
    void onTxFinished(YubiKeyIoTx::Result, const QByteArray&);

public:
    const YubiKeyIo::APDU iApdu;
    const Flags iFlags;
    const Priority iPriority;
    const int iId;
    bool iTxFinished;
    TxScopedPointer iTx;
    YubiKeyIoTx::Result iTxResult;
    QByteArray iTxRespBuf;
    OpData* iOpData;
    OpState iPrevOpState;
    OpState iOpState;
};

// ==========================================================================
// YubiKeyOpQueue::Private
// ==========================================================================

enum YubiKeyOpQueueSignal {
    #define SIGNAL_ENUM_(Name,name,Suffix) Signal##Name##Suffix,
    QUEUED_SIGNALS(SIGNAL_ENUM_)
    #undef  SIGNAL_ENUM_
    YubiKeyOpQueueSignalCount
};

typedef HarbourParentSignalQueueObject<YubiKeyOpQueue,
    YubiKeyOpQueueSignal, YubiKeyOpQueueSignalCount>
    YubiKeyOpQueuePrivateBase;

class YubiKeyOpQueue::Private :
    public YubiKeyOpQueuePrivateBase,
    public YubiKeyConstants
{
    Q_OBJECT

    friend class Entry;
    static const SignalEmitter gSignalEmitters[];
    typedef bool (YubiKeyIo::APDU::*MatchFn)(const YubiKeyIo::APDU&) const;

public:
    typedef QQueue<Entry*> Queue;
    typedef QListIterator<Entry*> Iterator;
    typedef QMutableListIterator<Entry*> MutableIterator;

    Private(YubiKeyOpQueue*);
    ~Private();

    void queueSetupSignal(YubiKeyOpQueueSignal);
    void emitQueuedSignals();
    void clear();
    void releaseIo();
    void setState(State);
    void setAuthAccess(YubiKeyAuthAccess);
    void setYubiKeySerial(uint);
    void setYubiKeyId(const QByteArray&);
    void setFwVersion(const QByteArray&);

    bool haveKeySpecificOp();
    void tryToStartNextOp();
    void startNextOp();
    void requeueActiveOp();
    void activeOpFailed();
    YubiKeyOp* lookup(int);
    YubiKeyOp* queue(Entry*);
    YubiKeyOp* queue(const YubiKeyIo::APDU&, Flags, Priority, YubiKeyOp::OpData*);
    int drop(const YubiKeyIo::APDU&, MatchFn);

    QList<int> opIds();
    void setIo(YubiKeyIo*);
    bool setInternalTx(YubiKeyIoTx*);
    void setPassword(const QString&, bool);
    void resetInternalTx();
    void revalidate();
    void prepare();
    void authorized();
    void selectOath();
    void selectOtp();
    YubiKeyAlgorithm authAlgorithm() const;
    QByteArray calculateAuthAccessKey(const QString&);
    QByteArray calculateAuthResponse(const QByteArray&, const QByteArray&);
    YubiKeyIo::APDU makeValidateApdu(const QByteArray&);
    void validate(const QByteArray&);

public Q_SLOTS:
    void onIoStateChanged(YubiKeyIo::IoState);
    void onIoDestroyed(QObject*);
    void onGetSerialFailed();
    void onSelectOtpFinished(YubiKeyIoTx::Result, const QByteArray&);
    void onGetSerialFinished(YubiKeyIoTx::Result, const QByteArray&);
    void onSelectOathFailed();
    void onSelectOathFinished(YubiKeyIoTx::Result, const QByteArray&);
    void onValidateFailed();
    void onValidateFinished(YubiKeyIoTx::Result, const QByteArray&);
    void onActiveOpStateChanged();

public:
    State iState;
    Queue iQueue;
    Entry* iActiveOp;
    QPointer<YubiKeyIo> iIo;
    TxScopedPointer iInternalTx;
    YubiKeyAuth iAuth;
    YubiKeyIo::IoLock iLock;
    QByteArray iAuthChallenge;
    YubiKeyAlgorithm iAuthAlgorithm;
    YubiKeyAuthAccess iAuthAccess;
    uint iYubiKeySerial;
    QByteArray iHostChallenge;
    QByteArray iYubiKeyId;
    QByteArray iFwVersion;
    bool iIoSetupDone;
    bool iRevalidate;
};

/* static */
const YubiKeyOpQueue::Private::SignalEmitter
YubiKeyOpQueue::Private::gSignalEmitters [] = {
    #define SIGNAL_EMITTER_(Name,name,Suffix) &YubiKeyOpQueue::name##Suffix,
    QUEUED_SIGNALS(SIGNAL_EMITTER_)
    #undef  SIGNAL_EMITTER_
};

YubiKeyOpQueue::Private::Private(
    YubiKeyOpQueue* aQueue) :
    YubiKeyOpQueuePrivateBase(aQueue, gSignalEmitters),
    iState(QueueIdle),
    iActiveOp(Q_NULLPTR),
    iAuthAlgorithm(YubiKeyAlgorithm_Unknown),
    iAuthAccess(YubiKeyAuthAccessUnknown),
    iYubiKeySerial(0),
    iIoSetupDone(false),
    iRevalidate(false)
{}

YubiKeyOpQueue::Private::~Private()
{
    resetInternalTx();
    delete iActiveOp;
    qDeleteAll(iQueue);
}

inline
YubiKeyAlgorithm
YubiKeyOpQueue::Private::authAlgorithm() const
{
    return (iAuthAlgorithm == YubiKeyAlgorithm_Unknown) ?
        YubiKeyAlgorithm_Default : iAuthAlgorithm;
}

void
YubiKeyOpQueue::Private::queueSetupSignal(
    YubiKeyOpQueueSignal aSignal)
{
    if (!iIoSetupDone) {
        iIoSetupDone = true;
        queueSignal(aSignal);
    }
}

void
YubiKeyOpQueue::Private::setAuthAccess(
    YubiKeyAuthAccess aAuthAccess)
{
    if (iAuthAccess != aAuthAccess) {
        HDEBUG(iAuthAccess << "=>" << aAuthAccess);
        iAuthAccess = aAuthAccess;
        queueSignal(SignalYubiKeyAuthAccessChanged);
    }
}

YubiKeyOp*
YubiKeyOpQueue::Private::lookup(
    int aId)
{
    if (aId) {
        if (iActiveOp && iActiveOp->iId == aId) {
            return iActiveOp;
        } else for (Iterator it(iQueue); it.hasNext();) {
            Entry* entry = it.next();

            if (entry->iId ==  aId) {
                return entry;
            }
        }
    }
    return Q_NULLPTR;
}

QList<int>
YubiKeyOpQueue::Private::opIds()
{
    QList<int> ids;

    ids.reserve(iQueue.size() + (iActiveOp ? 1 : 0));
    if (iActiveOp) {
        ids.append(iActiveOp->iId);
    }
    for (Iterator it(iQueue); it.hasNext();) {
        ids.append(it.next()->iId);
    }
    return ids;
}

bool
YubiKeyOpQueue::Private::haveKeySpecificOp()
{
    if (iActiveOp && (iActiveOp->iFlags & KeySpecific)) {
        return true;
    } else for (Iterator it(iQueue); it.hasNext();) {
        if (it.next()->iFlags & KeySpecific) {
            return true;
        }
    }
    return false;
}

void
YubiKeyOpQueue::Private::requeueActiveOp()
{
    if (iActiveOp) {
        Entry* op = iActiveOp;

        // Insert the operation back into the list based on its priority
        for (MutableIterator it(iQueue); it.hasNext();) {
            if (it.next()->iPriority <= iActiveOp->iPriority) {
                it.previous();
                it.insert(iActiveOp);
                iActiveOp = Q_NULLPTR;
                break;
            }
        }

        if (iActiveOp) {
            // Either the queue is empty or all the ops in the queue have
            // higher priority. In any case, the new one goes to the end
            // of the queue.
            iQueue.append(iActiveOp);
            iActiveOp = Q_NULLPTR;
        }

        op->disconnect(this);
        op->setOpState(YubiKeyOp::OpQueued);
    }
}

YubiKeyOp*
YubiKeyOpQueue::Private::queue(
    Entry* aEntry)
{
    // Insert the operation into the list based on its priority
    if (aEntry->iPriority > MinimumPriority) {
        for (MutableIterator it(iQueue); it.hasNext();) {
            if (it.next()->iPriority < aEntry->iPriority) {
                // Insert it before the first op with a smaller priority
                HDEBUG(aEntry->iId << aEntry->iApdu.name);
                it.previous();
                it.insert(aEntry);
                return aEntry;
            }
        }
    }

    // Either the queue is empty or all the ops in the queue have the same
    // or higher priority. In any case, the new one goes to the end of the
    // queue.
    HDEBUG(aEntry->iId << aEntry->iApdu.name);
    iQueue.append(aEntry);
    return aEntry;
}

YubiKeyOp*
YubiKeyOpQueue::Private::queue(
    const YubiKeyIo::APDU& aApdu,
    Flags aFlags,
    Priority aPriority,
    YubiKeyOp::OpData* aOpData)
{
    static int gLastId = 0;

    if (aFlags & Replace) {
        for (MutableIterator it(iQueue); it.hasNext();) {
            Entry* op = it.next();

            if (op->iApdu.sameAs(aApdu)) {
                HDEBUG("dropping queued" << op->iApdu.name <<
                    "command" << op->iId);
                it.remove();
                op->setOpState(YubiKeyOp::OpCancelled);
                HarbourUtil::scheduleDeleteLater(op);
                break;
            }
        }
    }

    int id = qMax(gLastId + 1, 1);

    // Pick a unique id
    while (lookup(id)) {
        id = qMax(id + 1, 1);
    }

    gLastId = id;

    queueSignal(SignalOpIdsChanged);
    return queue(new Entry(this, aApdu, aFlags, aPriority, id, aOpData));
}

int
YubiKeyOpQueue::Private::drop(
    const YubiKeyIo::APDU& aApdu,
    MatchFn aMatch)
{
    int count = 0;

    for (MutableIterator it(iQueue); it.hasNext();) {
        Entry* op = it.next();

        if (((op->iApdu).*(aMatch))(aApdu)) {
            HDEBUG("dropping queued" << op->iApdu.name <<
                   "command" << op->iId);
            it.remove();
            op->setOpState(YubiKeyOp::OpCancelled);
            queueSignal(SignalOpIdsChanged);
            HarbourUtil::scheduleDeleteLater(op);
            count++;
        }
    }
    return count;
}

void
YubiKeyOpQueue::Private::clear()
{
    Queue queue;

    setIo(Q_NULLPTR);
    queue.swap(iQueue);
    if (iActiveOp) {
        YubiKeyOp* activeOp = iActiveOp;

        iActiveOp = Q_NULLPTR;
        delete activeOp;
    }
    qDeleteAll(queue);
    iAuth.clear();
    iAuthAlgorithm = YubiKeyAlgorithm_Unknown;
    setYubiKeySerial(0);
    setYubiKeyId(QByteArray());
    setFwVersion(QByteArray());
    setAuthAccess(YubiKeyAuthAccessUnknown);
    setState(QueueIdle);
}

void
YubiKeyOpQueue::Private::releaseIo()
{
    if (iActiveOp) {
        iActiveOp->resetTx();
        requeueActiveOp();
    }
    resetInternalTx();
    iIoSetupDone = false;
    iRevalidate = false;
    iAuthChallenge.clear();
    iLock.reset();
}

void
YubiKeyOpQueue::Private::setState(
    State aState)
{
    if (iState != aState) {
        HDEBUG(iState << "=>" << aState);
        switch (iState = aState) {
        case QueueIdle:
        case QueueBlocked:
            releaseIo();
            break;
        case QueuePrepare:
        case QueueActive:
            break;
        }
        queueSignal(SignalOpQueueStateChanged);
    }
}

void
YubiKeyOpQueue::Private::startNextOp()
{
    if (iIo && iIo->canTransmit() && !iActiveOp && !iQueue.isEmpty()) {
        if (iRevalidate) {
            iRevalidate = false;
            selectOath();
            setState(QueuePrepare);
            return;
        } else {
            iActiveOp = iQueue.takeFirst();
            if (iActiveOp->start()) {
                connect(iActiveOp, SIGNAL(opStateChanged()),
                    SLOT(onActiveOpStateChanged()));
            } else {
                requeueActiveOp();
            }
        }
    }
    setState(iActiveOp ? QueueActive : QueueIdle);
}

void
YubiKeyOpQueue::Private::tryToStartNextOp()
{
    switch (iState) {
    case QueuePrepare:
    case QueueBlocked:
        break;
    case QueueIdle:
        if (iIo && iIo->canTransmit() && !iQueue.isEmpty()) {
            prepare();
        }
        break;
    case QueueActive:
        startNextOp();
        break;
    }
}

void
YubiKeyOpQueue::Private::prepare()
{
    if (iState != QueuePrepare) {
        setState(QueuePrepare);
        const bool alreadyLocked = (iIo->ioState() == YubiKeyIo::IoLocked);
        // If not already locked, ioLock() may change the state to IoLocked
        iLock = iIo->ioLock();
        if (alreadyLocked) {
            if (iYubiKeySerial) {
                selectOath();
            } else {
                // The I/O failed to provide S/N, try to figure it out
                selectOtp();
            }
        }
    }
}

void
YubiKeyOpQueue::Private::setIo(
    YubiKeyIo* aIo)
{
    if (iIo != aIo) {
        releaseIo();
        if (iIo) {
            iIo->disconnect(this);
        }
        iIo = aIo;
        setState(QueueIdle);
        if (aIo) {
            connect(aIo, SIGNAL(destroyed(QObject*)),
                SLOT(onIoDestroyed(QObject*)));
            connect(aIo, SIGNAL(ioStateChanged(YubiKeyIo::IoState)),
                SLOT(onIoStateChanged(YubiKeyIo::IoState)));

            // Even if the queue is empty, still lock and submit the SELECT
            prepare();
        }
    }
}

bool
YubiKeyOpQueue::Private::setInternalTx(
    YubiKeyIoTx* aTx)
{
    if (iInternalTx) {
        iInternalTx->disconnect(this);
        iInternalTx->txCancel();
    }
    iInternalTx.reset(aTx);
    return aTx != Q_NULLPTR;
}

void
YubiKeyOpQueue::Private::resetInternalTx()
{
    setInternalTx(Q_NULLPTR);
}

void
YubiKeyOpQueue::Private::setYubiKeySerial(
    uint aSerial)
{
    if (iYubiKeySerial != aSerial) {
        iYubiKeySerial = aSerial;
        queueSignal(SignalYubiKeySerialChanged);
    }
}

void
YubiKeyOpQueue::Private::setYubiKeyId(
    const QByteArray& aYubiKeyId)
{
    if (iYubiKeyId != aYubiKeyId) {
        iYubiKeyId = aYubiKeyId;
        iAuth = YubiKeyAuth(iYubiKeyId);
        queueSignal(SignalYubiKeyIdChanged);
    }
}

void
YubiKeyOpQueue::Private::setFwVersion(
    const QByteArray& aVersion)
{
    if (iFwVersion != aVersion) {
        iFwVersion = aVersion;
        queueSignal(SignalYubiKeyFwVersionChanged);
    }
}

void
YubiKeyOpQueue::Private::setPassword(
    const QString& aPassword,
    bool aSave)
{
    const YubiKeyAlgorithm alg = authAlgorithm();
    iAuth.setPassword(alg, aPassword, aSave);
    const QByteArray accessKey(iAuth.getAccessKey(alg));

#if HARBOUR_DEBUG
    if (accessKey.isEmpty()) {
        HDEBUG("trying open access");
    } else {
        HDEBUG("trying access key" << accessKey.toHex().constData());
    }
#endif // HARBOUR_DEBUG
    revalidate();
}

void
YubiKeyOpQueue::Private::revalidate()
{
    switch (iState) {
    case QueueIdle:
    case QueueBlocked:
        if (iIo) {
            prepare();
        }
        break;
    case QueuePrepare:
        // Validation is already in progress
        break;
    case QueueActive:
        iRevalidate = true;
        break;
    }
}

void
YubiKeyOpQueue::Private::selectOtp()
{
    static const uchar AID_OTP[] = {
        0xa0, 0x00, 0x00, 0x05, 0x27, 0x20, 0x01, 0x01
    };
    static const YubiKeyIo::APDU CMD_SELECT_OTP("SELECT",
        0x00, 0xa4, 0x04, 0x00, AID_OTP, sizeof(AID_OTP));

    resetInternalTx();
    requeueActiveOp();
    if (setInternalTx(iIo->ioTransmit(CMD_SELECT_OTP))) {
        YubiKeyIoTx* tx = iInternalTx.data();

        connect(tx, SIGNAL(txFailed()), SLOT(onGetSerialFailed()));
        connect(tx, SIGNAL(txCancelled()), SLOT(onGetSerialFailed()));
        connect(tx, SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
            SLOT(onSelectOtpFinished(YubiKeyIoTx::Result,QByteArray)));
    } else {
        selectOath();
    }
}

void
YubiKeyOpQueue::Private::onSelectOtpFinished(
    YubiKeyIoTx::Result aResult,
    const QByteArray&)
{
    resetInternalTx();
    if (aResult.success()) {
        static const YubiKeyIo::APDU CMD_GET_SERIAL("GET_SERIAL", 0x00, 0x01, 0x10);

        HDEBUG("SELECT ok");
        if (setInternalTx(iIo->ioTransmit(CMD_GET_SERIAL))) {
            YubiKeyIoTx* tx = iInternalTx.data();

            connect(tx, SIGNAL(txFailed()), SLOT(onGetSerialFailed()));
            connect(tx, SIGNAL(txCancelled()), SLOT(onGetSerialFailed()));
            connect(tx, SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
                SLOT(onGetSerialFinished(YubiKeyIoTx::Result,QByteArray)));
        } else {
            selectOath();
        }
    } else {
        HDEBUG("SELECT error" << aResult);
        selectOath();
    }
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onGetSerialFinished(
    YubiKeyIoTx::Result aResult,
    const QByteArray& aData)
{
    resetInternalTx();
    if (aResult.success()) {
        uint sn = 0;

        for (int i = 0; i < aData.size(); i++) {
            sn = (sn << 8) | (uchar) aData.at(i);
        }
        HDEBUG("GET_SERIAL ok" << aData.toHex().constData() << "=>" << sn);
        setYubiKeySerial(sn);
    } else {
        HDEBUG("GET_SERIAL error" << aResult);
    }
    selectOath();
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onGetSerialFailed()
{
    HDEBUG("Failed to query S/N");
    resetInternalTx();
    selectOath();
}

void
YubiKeyOpQueue::Private::selectOath()
{
    static const uchar AID_OATH[] = {
        0xa0, 0x00, 0x00, 0x05, 0x27, 0x21, 0x01
    };
    static const YubiKeyIo::APDU CMD_SELECT_OATH("SELECT",
        0x00, 0xa4, 0x04, 0x00, AID_OATH, sizeof(AID_OATH));

    resetInternalTx();
    requeueActiveOp();
    if (setInternalTx(iIo->ioTransmit(CMD_SELECT_OATH))) {
        YubiKeyIoTx* tx = iInternalTx.data();

        // SELECT requires special handling upon completion
        connect(tx, SIGNAL(txFailed()), SLOT(onSelectOathFailed()));
        connect(tx, SIGNAL(txCancelled()), SLOT(onSelectOathFailed()));
        connect(tx, SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
            SLOT(onSelectOathFinished(YubiKeyIoTx::Result,QByteArray)));
    } else {
        setState(QueueIdle);
    }
}

void
YubiKeyOpQueue::Private::authorized()
{
    // When we get authorized for the first time, issue the yubiKeyConnected
    // signal right away, to give the handler a chance to queue the LIST
    // command before we switch to the Idle state and release the lock
    // (because after re-acquiring the lock, we would have to go through
    // the same SELECT/VALIDATE sequence again, which would slow things down)
    queueSetupSignal(SignalYubiKeyConnected);
    emitQueuedSignal(SignalYubiKeyConnected);
}

QByteArray
YubiKeyOpQueue::Private::calculateAuthAccessKey(
    const QString& aPassword)
{
    return YubiKeyAuth::calculateAccessKey(iYubiKeyId,
        authAlgorithm(), aPassword);
}

QByteArray
YubiKeyOpQueue::Private::calculateAuthResponse(
    const QByteArray& aAccessKey,
    const QByteArray& aChallenge)
{
    return YubiKeyAuth::calculateResponse(aAccessKey,
        aChallenge, authAlgorithm());
}

YubiKeyIo::APDU
YubiKeyOpQueue::Private::makeValidateApdu(
    const QByteArray& aAccessKey)
{
    //
    // https://developers.yubico.com/OATH/YKOATH_Protocol.html
    //
    // VALIDATE INSTRUCTION
    //
    // Validates authentication (mutually). The challenge for this comes
    // from the SELECT command. The response if computed by performing
    // the correct HMAC function of that challenge with the correct key.
    // A new challenge is then sent to the application, together with
    // the response. The application will then respond with a similar
    // calculation that the host software can verify.
    //
    static const YubiKeyIo::APDU CMD_VALIDATE("VALIDATE", 0x00, 0xa3);

    const QByteArray response(calculateAuthResponse(aAccessKey, iAuthChallenge));
    iHostChallenge = YubiKeyUtil::randomAuthChallenge();
    HDEBUG("Response:" << response.toHex().constData());
    HDEBUG("Host challenge:" << iHostChallenge.toHex().constData());

    // Validate Data
    //
    // +------------------+---------------------+
    // | Response tag     | 0x75                |
    // | Response length  | Length of response  |
    // | Response data    | Response            |
    // +------------------+---------------------+
    // | Challenge tag    | 0x74                |
    // | Challenge length | Length of challenge |
    // | Challenge data   | Challenge           |
    // +------------------+---------------------+
    YubiKeyIo::APDU apdu(CMD_VALIDATE);

    apdu.data.reserve(ACCESS_KEY_LEN + CHALLENGE_LEN + 4);
    apdu.appendTLV(TLV_TAG_RESPONSE_FULL, response);
    apdu.appendTLV(TLV_TAG_CHALLENGE, iHostChallenge);
    return apdu;
}

void
YubiKeyOpQueue::Private::validate(
    const QByteArray& aAccessKey)
{
    resetInternalTx();
    requeueActiveOp();
    if (setInternalTx(iIo->ioTransmit(makeValidateApdu(aAccessKey)))) {
        YubiKeyIoTx* tx = iInternalTx.data();

        connect(tx, SIGNAL(txFailed()), SLOT(onValidateFailed()));
        connect(tx, SIGNAL(txCancelled()), SLOT(onValidateFailed()));
        connect(tx, SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
            SLOT(onValidateFinished(YubiKeyIoTx::Result,QByteArray)));
    } else {
        setState(QueueIdle);
    }
}

void
YubiKeyOpQueue::Private::onActiveOpStateChanged()
{
    Entry* op = qobject_cast<Entry*>(sender());

    HASSERT(op == iActiveOp);
    switch (op->opState()) {
    case YubiKeyOp::OpQueued:
    case YubiKeyOp::OpActive:
        // Keep it around
        return;
    case YubiKeyOp::OpCancelled:
    case YubiKeyOp::OpFinished:
        break;
    case YubiKeyOp::OpFailed:
        if (op->iFlags & Retry) {
            // Keep it around
            HDEBUG(op->name() << "failed, will retry");
            requeueActiveOp();
            tryToStartNextOp();
            emitQueuedSignals();
            op->emitQueuedSignals();
            return;
        } else {
            HDEBUG(op->name() << "failed");
            break;
        }
    }

    op->disconnect(this);
    iActiveOp = Q_NULLPTR;

    queueSignal(SignalOpIdsChanged);
    tryToStartNextOp();
    emitQueuedSignals();
    op->emitQueuedSignals();
    HarbourUtil::scheduleDeleteLater(op);
}

void
YubiKeyOpQueue::Private::onIoStateChanged(
    YubiKeyIo::IoState /* previous */)
{
    if (iIo->ioState() == YubiKeyIo::IoLocked && iState == QueuePrepare) {
        if (iYubiKeySerial) {
            selectOath();
        } else {
            // Try to figure out the S/N first
            selectOtp();
        }
    }
}

void
YubiKeyOpQueue::Private::onIoDestroyed(
    QObject*)
{
    HDEBUG("I/O is gone");
    setState(QueueIdle);
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onSelectOathFailed()
{
    HDEBUG("SELECT failed");
    resetInternalTx();
    setState(QueueIdle);
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onValidateFailed()
{
    HDEBUG("VALIDATE failed");
    resetInternalTx();
    setState(QueueIdle);
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onSelectOathFinished(
    YubiKeyIoTx::Result aResult,
    const QByteArray& aData)
{
    resetInternalTx();
    if (aResult.success()) {
        HDEBUG("SELECT ok");
        YubiKeyUtil::SelectResponse response;
        if (response.parse(aData)) {
            if (haveKeySpecificOp() && iYubiKeyId != response.cardId) {
                HDEBUG("id mismatch" << iYubiKeyId.toHex().constData() <<
                    "vs" << response.cardId.toHex().constData());
                queueSetupSignal(SignalInvalidYubiKeyConnected);
                setState(QueueBlocked);
            } else {
                setYubiKeyId(response.cardId);
                setFwVersion(response.version);
                iAuthAlgorithm = response.authAlg;
                iAuthChallenge = response.authChallenge;

                if (iAuthChallenge.isEmpty()) {
                    // Authorization isn't required
                    iAuth.forgetPassword();
                    setAuthAccess(YubiKeyAuthAccessOpen);
                    authorized();
                    startNextOp();
                } else {
                    // See if we have the access key for it
                    QByteArray accessKey(iAuth.getAccessKey(iAuthAlgorithm));
                    if (accessKey.isEmpty()) {
                        if (haveKeySpecificOp()) {
                            queueSetupSignal(SignalInvalidYubiKeyConnected);
                            setState(QueueBlocked);
                        } else {
                            // No access key, will have to ask the user
                            setAuthAccess(YubiKeyAuthAccessDenied);
                            HDEBUG("No" << iAuthAlgorithm << "access key for" <<
                                iYubiKeyId.toHex().constData());
                            setState(QueueIdle);
                        }
                    } else {
                        validate(accessKey);
                    }
                }
            }
        } else {
            HDEBUG("Failed to parse the SELECT response");
            setState(QueueIdle);
            setYubiKeyId(QByteArray());
            setFwVersion(QByteArray());
            iAuthChallenge.clear();
        }
    } else {
        setState(QueueIdle);
        setYubiKeyId(QByteArray());
        setFwVersion(QByteArray());
        iAuthChallenge.clear();

        if (aResult.code == YubiKeyConstants::RC_FILE_NOT_FOUND) {
            // We still transition to the ready state because 6a82 from
            // SELECT means that the YubiKey hasn't been activated yet
            // (if this if a YubiKey)
            HDEBUG("SELECT error" << aResult << "(Activation is required)");
            if (haveKeySpecificOp()) {
                queueSetupSignal(SignalInvalidYubiKeyConnected);
                setState(QueueBlocked);
            } else {
                setAuthAccess(YubiKeyAuthAccessNotActivated);
                queueSetupSignal(SignalRestrictedYubiKeyConnected);
            }
        } else {
            HDEBUG("SELECT error" << aResult);
            setAuthAccess(YubiKeyAuthAccessUnknown);
        }
    }
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onValidateFinished(
    YubiKeyIoTx::Result aResult,
    const QByteArray& aData)
{
    resetInternalTx();

    // Response Codes
    //
    // +------------------+--------+
    // | Success          | 0x9000 |
    // +------------------+--------+
    // | Auth not enabled | 0x6984 |
    // +------------------+--------+
    // | Wrong syntax     | 0x6a80 |
    // +------------------+--------+
    // | Generic error    | 0x6581 |
    // +------------------+--------+
    if (aResult.success()) {
        QByteArray cardResp;
        GUtilRange resp;
        GUtilData data;
        uchar tag;

        HDEBUG("VALIDATE ok");
        YubiKeyUtil::initRange(resp, aData);
        while ((tag = YubiKeyUtil::readTLV(&resp, &data)) != 0) {
            if (tag == TLV_TAG_RESPONSE_FULL) {
                cardResp = QByteArray((char*)data.bytes, data.size);
                HDEBUG("Card response:" << cardResp.toHex().constData());
                break;
            }
        }

        // Validate the response
        if (!cardResp.isEmpty()) {
            const QByteArray hostResp(iAuth.calculateResponse(iHostChallenge,
                iAuthAlgorithm));

            HDEBUG("Host response:" << hostResp.toHex().constData());
            if (hostResp == cardResp) {
                HDEBUG("Match!");
                iAuth.touch();
                setAuthAccess(YubiKeyAuthAccessGranted);
                authorized();
                startNextOp();
            } else {
                queueSetupSignal(SignalYubiKeyValidationFailed);
                setAuthAccess(YubiKeyAuthAccessDenied);
                setState(QueueIdle);
            }
        } else {
            setAuthAccess(YubiKeyAuthAccessDenied);
            setState(QueueIdle);
        }
    } else {
        queueSetupSignal(SignalYubiKeyValidationFailed);
        setAuthAccess(YubiKeyAuthAccessDenied);
        setState(QueueIdle);
    }
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::emitQueuedSignals()
{
    if (iActiveOp) {
        iActiveOp->emitQueuedSignals();
    }

    if (!iQueue.isEmpty()) {
        Queue changedOps;

        // Protect against iQueue modifications by the signal handlers
        for (Iterator it(iQueue); it.hasNext();) {
            Entry* op = it.next();

            if (op->hasQueuedSignals()) {
                changedOps.append(op);
            }
        }
        if (!changedOps.isEmpty()) {
            for (Iterator it(changedOps); it.hasNext();) {
                it.next()->emitQueuedSignals();
            }
        }
    }
    YubiKeyOpQueuePrivateBase::emitQueuedSignals();
}

// ==========================================================================
// YubiKeyOpQueue::Entry
// ==========================================================================

YubiKeyOpQueue::Entry::Entry(
    Private* aPrivate,
    const YubiKeyIo::APDU& aApdu,
    Flags aFlags,
    Priority aPriority,
    int aId,
    OpData* aOpData) :
    YubiKeyOp(aPrivate),
    iApdu(aApdu),
    iFlags(aFlags),
    iPriority(aPriority),
    iId(aId),
    iTxFinished(false),
    iOpData(aOpData),
    iPrevOpState(OpQueued),
    iOpState(OpQueued)
{}

YubiKeyOpQueue::Entry::~Entry()
{
    resetTx();
    setOpState(OpCancelled);
    emitQueuedSignals();
    delete iOpData;
}

inline
YubiKeyOpQueue::Private*
YubiKeyOpQueue::Entry::owner() const
{
    return qobject_cast<Private*>(parent());
}

void
YubiKeyOpQueue::Entry::setOpState(
    OpState aState)
{
    if (iOpState != aState) {
        switch (iOpState) {
        case OpQueued:
        case OpActive:
            break;
        case OpFailed:
            // Allow switching back from the OpFailed state
            // if the Retry flag is set
            if (iFlags & Retry) {
                switch (aState) {
                case OpQueued:
                case OpActive:
                    break;
                case OpFailed:
                case OpCancelled:
                case OpFinished:
                    return;
                }
                break;
            }
            //fallthrough
        case OpCancelled:
        case OpFinished:
            return;
        }
        HDEBUG(iId << iOpState << "=>" << aState);
        iOpState = aState;
    }
}

bool
YubiKeyOpQueue::Entry::hasQueuedSignals() const
{
    return iTxFinished || iPrevOpState != iOpState;
}

void
YubiKeyOpQueue::Entry::emitQueuedSignals()
{
    if (iTxFinished) {
        const uint code = iTxResult.code;
        QByteArray data;

        // Clear the state before emitting the signal
        iTxFinished = false;
        iTxResult.code = 0;
        iTxRespBuf.swap(data);
        Q_EMIT opFinished(code, data);
    }
    if (iPrevOpState != iOpState) {
        iPrevOpState = iOpState;
        Q_EMIT opStateChanged();
    }
}

const char*
YubiKeyOpQueue::Entry::name() const
{
    return iApdu.name;
}

YubiKeyOp::OpData*
YubiKeyOpQueue::Entry::opData() const
{
    return iOpData;
}

YubiKeyOp::OpState
YubiKeyOpQueue::Entry::opState() const
{
    return iOpState;
}

int
YubiKeyOpQueue::Entry::opId() const
{
    return iId;
}

void
YubiKeyOpQueue::Entry::opCancel()
{
    Private* p = owner();

    if (iTx) {
        // Cancel the transaction and wait until it actually gets cancelled
        // The op remains active for the time being.
        HASSERT(p->iActiveOp == this);
        HASSERT(iOpState == OpActive);
        iTx->txCancel();
    } else {
        HASSERT(iOpState != OpActive);
        switch (iOpState) {
        case OpQueued:
            HVERIFY(p->iQueue.removeOne(this));
            p->queueSignal(SignalOpIdsChanged);
            setOpState(OpCancelled);
            break;
        case OpActive:
        case OpCancelled:
        case OpFinished:
        case OpFailed:
            break;
        }
        // We are done with this op
        HarbourUtil::scheduleDeleteLater(this);
    }

    emitQueuedSignals();
    p->emitQueuedSignals();
}

bool
YubiKeyOpQueue::Entry::opIsDone() const
{
    switch (iOpState) {
    case OpQueued:
    case OpActive:
        break;
    case OpFailed:
        // Allow switching back from the OpFailed state
        // if the Retry flag is set
        return (!(iFlags & Retry));
    case OpCancelled:
    case OpFinished:
        return true;
    }
    return false;
}

bool
YubiKeyOpQueue::Entry::start()
{
    Private* p = owner();

    HASSERT(!iTxFinished);
    iTxRespBuf.resize(0);
    if (iTx) {
        iTx->disconnect(this);
        iTx->txCancel();
    }
    if (p->iIo && setTx(p->iIo->ioTransmit(iApdu))) {
        connect(iTx.data(),
            SIGNAL(txCancelled()),
            SLOT(onTxCancelled()));
        connect(iTx.data(),
            SIGNAL(txFailed()),
            SLOT(onTxFailed()));
        connect(iTx.data(),
            SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
            SLOT(onTxFinished(YubiKeyIoTx::Result,QByteArray)));
        setOpState(OpActive);
        return true;
    }
    return false;
}

bool
YubiKeyOpQueue::Entry::setTx(
    YubiKeyIoTx* aTx)
{
    if (iTx) {
        iTx->disconnect(this);
        iTx->txCancel();
    }
    iTx.reset(aTx);
    if (aTx) {
        connect(aTx, SIGNAL(txCancelled()), SLOT(onTxCancelled()));
        connect(aTx, SIGNAL(txFailed()), SLOT(onTxFailed()));
        connect(aTx, SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
            SLOT(onTxFinished(YubiKeyIoTx::Result,QByteArray)));
    }
    return aTx != Q_NULLPTR;
}

void
YubiKeyOpQueue::Entry::resetTx()
{
    setTx(Q_NULLPTR);
}

void
YubiKeyOpQueue::Entry::sendRemaining(
    uint aAmount)
{
    static const YubiKeyIo::APDU SEND_REMAINING("SEND_REMAINING", 0x00, 0xa5);

    Private* p = owner();
    YubiKeyIo* io = p->iIo;

    HDEBUG(iTxRespBuf.size() << "bytes +" << aAmount << "more");
    if (!io || !setTx(io->ioTransmit(SEND_REMAINING))) {
        setOpState(OpFailed);
    }
}


void
YubiKeyOpQueue::Entry::onTxCancelled()
{
    resetTx();
    setOpState(OpCancelled);
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Entry::onTxFailed()
{
    resetTx();
    setOpState(OpFailed);
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Entry::onTxFinished(
    YubiKeyIoTx::Result aResult,
    const QByteArray& aData)
{
    uint amount;

    iTx->disconnect(this);
    iTx->txCancel();
    iTx.reset();

    iTxRespBuf.append(aData);
    if (aResult.moreData(&amount)) {
        HDEBUG(name() << "(partial)" << aData.size() << "bytes");
        sendRemaining(amount);
    } else {
#if HARBOUR_DEBUG
        if (aResult.success()) {
            HDEBUG(name() << "ok" << iTxRespBuf.toHex().constData());
        } else {
            HDEBUG(name() << "error" << aResult);
        }
#endif // HARBOUR_DEBUG
        iTxFinished = true;
        iTxResult = aResult;
        setOpState(OpFinished);
    }
    emitQueuedSignals();
}

// ==========================================================================
// YubiKeyOpQueue
// ==========================================================================

YubiKeyOpQueue::YubiKeyOpQueue(
    QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{}

void
YubiKeyOpQueue::setIo(
    YubiKeyIo* aIo)
{
    iPrivate->setIo(aIo);
    iPrivate->emitQueuedSignals();
}

void
YubiKeyOpQueue::setPassword(
    QString aPassword,
    bool aSave)
{
    iPrivate->setPassword(aPassword, aSave);
    iPrivate->emitQueuedSignals();
}

void
YubiKeyOpQueue::reinitialize()
{
    iPrivate->revalidate();
    iPrivate->emitQueuedSignals();
}

bool
YubiKeyOpQueue::isEmpty() const
{
    return iPrivate->iQueue.isEmpty();
}

void
YubiKeyOpQueue::clear()
{
    iPrivate->clear();
    iPrivate->emitQueuedSignals();
}

YubiKeyOp*
YubiKeyOpQueue::lookup(
    int aOpId) const
{
    return iPrivate->lookup(aOpId);
}

YubiKeyOp*
YubiKeyOpQueue::queue(
    const YubiKeyIo::APDU& aApdu)
{
    return queue(aApdu, Default, DefaultPriority, Q_NULLPTR);
}

YubiKeyOp*
YubiKeyOpQueue::queue(
    const YubiKeyIo::APDU& aApdu,
    Flags aFlags)
{
    return queue(aApdu, aFlags, DefaultPriority, Q_NULLPTR);
}

YubiKeyOp*
YubiKeyOpQueue::queue(
    const YubiKeyIo::APDU& aApdu,
    Flags aFlags,
    Priority aPriority)
{
    return queue(aApdu, aFlags, aPriority, Q_NULLPTR);
}

YubiKeyOp*
YubiKeyOpQueue::queue(
    const YubiKeyIo::APDU& aApdu,
    Flags aFlags,
    YubiKeyOp::OpData* aOpData)
{
    return queue(aApdu, aFlags, DefaultPriority, aOpData);
}

YubiKeyOp*
YubiKeyOpQueue::queue(
    const YubiKeyIo::APDU& aApdu,
    Flags aFlags,
    Priority aPriority,
    YubiKeyOp::OpData* aOpData)
{
    YubiKeyOp* op = iPrivate->queue(aApdu, aFlags, aPriority, aOpData);

    iPrivate->tryToStartNextOp();
    iPrivate->emitQueuedSignals();
    return op;
}

int
YubiKeyOpQueue::drop(
    const YubiKeyIo::APDU& aApdu,
    bool aFullMatch)
{
    const int count = iPrivate->drop(aApdu, aFullMatch ?
        &YubiKeyIo::APDU::equals :
        &YubiKeyIo::APDU::sameAs);

    iPrivate->emitQueuedSignals();
    return count;
}

YubiKeyOpQueue::State
YubiKeyOpQueue::opQueueState() const
{
    return iPrivate->iState;
}

QList<int>
YubiKeyOpQueue::opIds() const
{
    return iPrivate->opIds();
}

uint
YubiKeyOpQueue::yubiKeySerial() const
{
    return iPrivate->iYubiKeySerial;
}

QByteArray
YubiKeyOpQueue::yubiKeyId() const
{
    return iPrivate->iYubiKeyId;
}

QByteArray
YubiKeyOpQueue::yubiKeyFwVersion() const
{
    return iPrivate->iFwVersion;
}

YubiKeyAuthAccess
YubiKeyOpQueue::yubiKeyAuthAccess() const
{
    return iPrivate->iAuthAccess;
}

YubiKeyAlgorithm
YubiKeyOpQueue::yubiKeyAuthAlgorithm() const
{
    return iPrivate->authAlgorithm();
}

QByteArray
YubiKeyOpQueue::calculateAuthAccessKey(
    QString aPassword) const
{
    return iPrivate->calculateAuthAccessKey(aPassword);
}

#include "YubiKeyOpQueue.moc"
