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
    Entry* setOpState(OpState);
    const char* name() const;

    // YubiKeyOp
    OpData* opData() const Q_DECL_OVERRIDE;
    OpState opState() const Q_DECL_OVERRIDE;
    int opId() const Q_DECL_OVERRIDE;
    void opCancel() Q_DECL_OVERRIDE;

public:
    const YubiKeyIo::APDU iApdu;
    const Flags iFlags;
    const Priority iPriority;
    const int iId;
    OpData* iOpData;
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

public:
    typedef QQueue<Entry*> Queue;
    typedef QListIterator<Entry*> Iterator;
    typedef QMutableListIterator<Entry*> MutableIterator;
    typedef QScopedPointer<YubiKeyIoTx, HarbourUtil::ScopedPointerDeleteLater> TxScopedPointer;

    Private(YubiKeyOpQueue*);
    ~Private();

    void queueSetupSignal(YubiKeyOpQueueSignal);
    void clear();
    void releaseIo();
    void setState(State);
    void setAuthAccess(YubiKeyAuthAccess);
    void setYubiKeySerial(uint);
    void setYubiKeyId(const QByteArray&);
    void setFwVersion(const QByteArray&);

    bool haveKeySpecificOp();
    void tryToStartNextOp();
    State startNextOp();
    void cancelActiveOp();
    void requeueActiveOp();
    void activeOpFinished(uint, const QByteArray&);
    void activeOpFailed();
    YubiKeyOp* lookup(int);
    YubiKeyOp* queue(Entry*);
    YubiKeyOp* queue(const YubiKeyIo::APDU&, Flags, Priority, YubiKeyOp::OpData*);

    QList<int> opIds();
    void setIo(YubiKeyIo*);
    bool setTx(YubiKeyIoTx*);
    void setPassword(const QString&, bool);
    void resetTx();
    void revalidate();
    void prepare();
    void authorized();
    void select();
    YubiKeyAlgorithm authAlgorithm() const;
    QByteArray calculateAuthAccessKey(const QString&);
    QByteArray calculateAuthResponse(const QByteArray&, const QByteArray&);
    YubiKeyIo::APDU makeValidateApdu(const QByteArray&);
    void validate(const QByteArray&);
    void sendRemaining(uint);

public Q_SLOTS:
    void onIoStateChanged(YubiKeyIo::IoState);
    void onIoDestroyed(QObject*);
    void onSelectFailed();
    void onSelectFinished(YubiKeyIoTx::Result, const QByteArray&);
    void onValidateFailed();
    void onValidateFinished(YubiKeyIoTx::Result, const QByteArray&);
    void onTxFailed();
    void onTxFinished(YubiKeyIoTx::Result, const QByteArray&);
    void onRemainingFinished(YubiKeyIoTx::Result, const QByteArray&);

public:
    State iState;
    Queue iQueue;
    Entry* iActiveOp;
    QPointer<YubiKeyIo> iIo;
    TxScopedPointer iTx;
    QByteArray iRespBuf;
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
    resetTx();
    if (iActiveOp) {
        delete iActiveOp->setOpState(YubiKeyOp::OpCancelled);
    }
    for (Iterator it(iQueue); it.hasNext();) {
        delete it.next()->setOpState(YubiKeyOp::OpCancelled);
    }
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
YubiKeyOpQueue::Private::cancelActiveOp()
{
    iActiveOp->setOpState(YubiKeyOp::OpCancelled);
    HarbourUtil::scheduleDeleteLater(iActiveOp);
    iActiveOp = Q_NULLPTR;
    queueSignal(SignalOpIdsChanged);
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
                return;
            }
        }

        if (iActiveOp) {
            // Either the queue is empty or all the ops in the queue have
            // higher priority. In any case, the new one goes to the end
            // of the queue.
            iQueue.append(iActiveOp);
            iActiveOp = Q_NULLPTR;
        }

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
        if (iActiveOp && iActiveOp->iApdu.sameAs(aApdu)) {
            HDEBUG("cancelling active" << iActiveOp->iApdu.name <<
                "command" << iActiveOp->iId);
            cancelActiveOp();
        } else for (MutableIterator it(iQueue); it.hasNext();) {
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

void
YubiKeyOpQueue::Private::clear()
{
    setIo(Q_NULLPTR);
    if (iActiveOp) {
        delete iActiveOp->setOpState(YubiKeyOp::OpCancelled);
        iActiveOp = Q_NULLPTR;
    }
    for (Iterator it(iQueue); it.hasNext();) {
        delete it.next()->setOpState(YubiKeyOp::OpCancelled);
    }
    iQueue.clear();
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
    resetTx();
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

YubiKeyOpQueue::State
YubiKeyOpQueue::Private::startNextOp()
{
    if (iIo && iIo->canTransmit() && !iActiveOp && !iQueue.isEmpty()) {
        if (iRevalidate) {
            iRevalidate = false;
            select();
            return QueuePrepare;
        } else {
            iActiveOp = iQueue.takeFirst();
            if (setTx(iIo->ioTransmit(iActiveOp->iApdu))) {
                iActiveOp->setOpState(YubiKeyOp::OpActive);
                iRespBuf.resize(0);
                connect(iTx.data(),
                    SIGNAL(txCancelled()),
                    SLOT(onTxFailed()));
                connect(iTx.data(),
                    SIGNAL(txFailed()),
                    SLOT(onTxFailed()));
                connect(iTx.data(),
                    SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
                    SLOT(onTxFinished(YubiKeyIoTx::Result,QByteArray)));
            } else {
                requeueActiveOp();
            }
        }
    }
    return iActiveOp ? QueueActive : QueueIdle;
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
        setState(startNextOp());
        break;
    }
}

void
YubiKeyOpQueue::Private::prepare()
{
    if (iState != QueuePrepare) {
        setState(QueuePrepare);
        iLock = iIo->ioLock();
        if (iIo->ioState() == YubiKeyIo::IoLocked) {
            select();
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
YubiKeyOpQueue::Private::setTx(
    YubiKeyIoTx* aTx)
{
    if (iTx) {
        iTx->disconnect(this);
        iTx->txCancel();
    }
    iTx.reset(aTx);
    return aTx != Q_NULLPTR;
}

void
YubiKeyOpQueue::Private::resetTx()
{
    setTx(Q_NULLPTR);
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
YubiKeyOpQueue::Private::select()
{
    static const uchar AID[] = {0xa0, 0x00, 0x00, 0x05, 0x27, 0x21, 0x01};
    static const YubiKeyIo::APDU CMD_SELECT("SELECT",
        0x00, 0xa4, 0x04, 0x00, AID, sizeof(AID));

    resetTx();
    requeueActiveOp();
    if (setTx(iIo->ioTransmit(CMD_SELECT))) {
        // SELECT requires special handling upon completion
        connect(iTx.data(), SIGNAL(txFailed()), SLOT(onSelectFailed()));
        connect(iTx.data(), SIGNAL(txCancelled()), SLOT(onSelectFailed()));
        connect(iTx.data(), SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
            SLOT(onSelectFinished(YubiKeyIoTx::Result,QByteArray)));
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
    resetTx();
    requeueActiveOp();
    if (setTx(iIo->ioTransmit(makeValidateApdu(aAccessKey)))) {
        connect(iTx.data(), SIGNAL(txFailed()), SLOT(onValidateFailed()));
        connect(iTx.data(), SIGNAL(txCancelled()), SLOT(onValidateFailed()));
        connect(iTx.data(), SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
            SLOT(onValidateFinished(YubiKeyIoTx::Result,QByteArray)));
    } else {
        setState(QueueIdle);
    }
}

void
YubiKeyOpQueue::Private::sendRemaining(
    uint aAmount)
{
    static const YubiKeyIo::APDU SEND_REMAINING("SEND_REMAINING", 0x00, 0xa5);

    HDEBUG(iRespBuf.size() << "bytes +" << aAmount << "more");
    if (setTx(iIo->ioTransmit(SEND_REMAINING))) {
        connect(iTx.data(), SIGNAL(txCancelled()), SLOT(onTxFailed()));
        connect(iTx.data(), SIGNAL(txFailed()), SLOT(onTxFailed()));
        connect(iTx.data(),SIGNAL(txFinished(YubiKeyIoTx::Result,QByteArray)),
            SLOT(onRemainingFinished(YubiKeyIoTx::Result,QByteArray)));
    } else {
        setState(QueueIdle);
        requeueActiveOp();
    }
}

void
YubiKeyOpQueue::Private::activeOpFinished(
    uint aResult,
    const QByteArray& aData)
{
    Entry* op = iActiveOp;

#if HARBOUR_DEBUG
    if (aResult == RC_OK) {
        HDEBUG(op->name() << "ok" << aData.toHex().constData());
    } else {
        HDEBUG(op->name() << "error" << YubiKeyIoTx::Result(aResult));
    }
#endif // HARBOUR_DEBUG

    queueSignal(SignalOpIdsChanged);
    iActiveOp = Q_NULLPTR;

    Q_EMIT op->opFinished(aResult, aData);
    op->setOpState(YubiKeyOp::OpFinished);
    HarbourUtil::scheduleDeleteLater(op);
    tryToStartNextOp();
}

void
YubiKeyOpQueue::Private::activeOpFailed()
{
    Entry* op = iActiveOp;

    if (op->iFlags & Retry) {
        HDEBUG(op->name() << "failed, will retry");
        requeueActiveOp();
        tryToStartNextOp();
    } else {
        iActiveOp = Q_NULLPTR;
        HDEBUG(op->name() << "failed");
        op->setOpState(YubiKeyOp::OpFailed);
        HarbourUtil::scheduleDeleteLater(op);
    }
}

void
YubiKeyOpQueue::Private::onIoStateChanged(
    YubiKeyIo::IoState /* previous */)
{
    if (iIo->ioState() == YubiKeyIo::IoLocked && iState == QueuePrepare) {
        select();
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
YubiKeyOpQueue::Private::onSelectFailed()
{
    HDEBUG("SELECT failed");
    resetTx();
    setState(QueueIdle);
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onValidateFailed()
{
    HDEBUG("VALIDATE failed");
    resetTx();
    setState(QueueIdle);
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onSelectFinished(
    YubiKeyIoTx::Result aResult,
    const QByteArray& aData)
{
    resetTx();

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
                setYubiKeySerial(iIo->ioSerial());
                setYubiKeyId(response.cardId);
                setFwVersion(response.version);
                iAuthAlgorithm = response.authAlg;
                iAuthChallenge = response.authChallenge;

                if (iAuthChallenge.isEmpty()) {
                    // Authorization isn't required
                    iAuth.forgetPassword();
                    setAuthAccess(YubiKeyAuthAccessOpen);
                    authorized();
                    setState(startNextOp());
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
            setYubiKeySerial(0);
            setYubiKeyId(QByteArray());
            setFwVersion(QByteArray());
            iAuthChallenge.clear();
        }
    } else {
        setState(QueueIdle);
        setYubiKeySerial(0);
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
    resetTx();

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
                setState(startNextOp());
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
YubiKeyOpQueue::Private::onTxFailed()
{
    resetTx();
    activeOpFailed();
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onTxFinished(
    YubiKeyIoTx::Result aResult,
    const QByteArray& aData)
{
    uint amount;

    resetTx();
    if (aResult.moreData(&amount)) {
        HDEBUG(iActiveOp->name() << "(partial)" << aData.size() << "bytes");
        iRespBuf.append(aData);
        sendRemaining(amount);
    } else {
        activeOpFinished(aResult.code, aData);
    }
    emitQueuedSignals();
}

void
YubiKeyOpQueue::Private::onRemainingFinished(
    YubiKeyIoTx::Result aResult,
    const QByteArray& aData)
{
    uint amount;

    iRespBuf.append(aData);
    resetTx();

    if (aResult.moreData(&amount)) {
        sendRemaining(amount);
    } else {
        const QByteArray data(iRespBuf);

        iRespBuf.clear();
        HDEBUG("SEND_REMAINING done" << data.size() << "bytes");
        activeOpFinished(aResult.code, data);
    }
    emitQueuedSignals();
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
    iOpData(aOpData),
    iOpState(OpQueued)
{}

YubiKeyOpQueue::Entry::~Entry()
{
    delete iOpData;
}

inline
YubiKeyOpQueue::Private*
YubiKeyOpQueue::Entry::owner() const
{
    return qobject_cast<Private*>(parent());
}

YubiKeyOpQueue::Entry*
YubiKeyOpQueue::Entry::setOpState(
    OpState aState)
{
    if (!isDone() && iOpState != aState) {
        HDEBUG(iId << iOpState << "=>" << aState);
        iOpState = aState;
        Q_EMIT opStateChanged();
    }
    return this;
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

    switch (iOpState) {
    case OpQueued:
        HVERIFY(p->iQueue.removeOne(this));
        p->queueSignal(SignalOpIdsChanged);
        setOpState(OpCancelled);
        break;
    case OpActive:
        HASSERT(p->iActiveOp == this);
        p->cancelActiveOp();
        break;
    case OpCancelled:
    case OpFinished:
    case OpFailed:
        break;
    }

    p->emitQueuedSignals();
    HarbourUtil::scheduleDeleteLater(this);
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
