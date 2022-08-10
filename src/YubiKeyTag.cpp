/*
 * Copyright (C) 2022 Jolla Ltd.
 * Copyright (C) 2022 Slava Monich <slava@monich.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
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

#include "nfcdc_isodep.h"
#include "nfcdc_tag.h"

#include "gutil_log.h"
#include "gutil_misc.h"
#include "gutil_strv.h"

#include "YubiKeyConstants.h"
#include "YubiKeyTag.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"

#include <QAtomicInt>
#include <QMap>

#define TAG_STATES(s) \
    s(None) \
    s(Checking) \
    s(Unrecognized) \
    s(YubiKeyReady)

#define OP_STATES(s) \
    s(Init) \
    s(Queued) \
    s(Active) \
    s(Finished) \
    s(Cancelled) \
    s(Deleted)

#if HARBOUR_DEBUG
QDebug
operator<<(
    QDebug aDebug,
    YubiKeyTag::TagState aState)
{
    switch (aState) {
#define PRINT_STATE_(State) case YubiKeyTag::Tag##State: \
    return (aDebug << "Tag" #State);
    TAG_STATES(PRINT_STATE_)
#undef PRINT_STATE_
    }
    return (aDebug << (int)aState);
}
#  define REPORT_ERROR(name,sw,err) \
    ((void)((err) ? HDEBUG(name " error" << (err)->message) : \
    HDEBUG(name " error" << hex << sw)))
#else
#  define REPORT_ERROR(name,sw,err) ((void)0)
#endif // HARBOUR_DEBUG

#define DUMP_APDU(apdu) HDEBUG(hex << (apdu)->cla << \
    (apdu)->ins << (apdu)->p1 << (apdu)->p2 << \
    qPrintable(YubiKeyUtil::toHex(&(apdu)->data)))

// s(SignalName,signalName)
#define YUBIKEY_TAG_SIGNALS(s) \
    s(TagState,tagState) \
    s(OperationIds,operationIds) \
    s(YubiKeyVersion,yubiKeyVersion) \
    s(YubiKeyAuthChallenge,yubiKeyAuthChallenge) \
    s(YubiKeyAuthAlgorithm,yubiKeyAuthAlgorithm) \
    s(YubiKeyId,yubiKeyId)

// ==========================================================================
// YubiKeyTag::Operation::Private declaration
// ==========================================================================

class YubiKeyTag::Operation::Private:
    public YubiKeyConstants
{
public:
    class Queue {
    public:
        Queue() : iFirst(Q_NULLPTR), iLast(Q_NULLPTR) {}
        ~Queue() { clear(); }

        bool isEmpty() const;
        Operation* findEqual(Operation*);
        void push(Operation*);
        void pushFront(Operation*);
        void remove(Operation*);
        Operation* take();
        void clear();

    public:
        Operation* iFirst;
        Operation* iLast;
    };

    class TransmitData {
    public:
        TransmitData(Operation*, TransmitDone);
        ~TransmitData();
        static void staticResp(NfcIsoDepClient*,const GUtilData*, guint, const GError*, void*);
        static void staticFree(gpointer);
    public:
        Operation* iOperation;
        TransmitDone iCallback;
    };

    // None => Queued => Active => Finished
    // And can be cancelled at any point
    enum OpState {
#define DEFINE_STATE_(State) Op##State,
    OP_STATES(DEFINE_STATE_)
#undef DEFINE_STATE_
    };

    Private(Operation*, const char*, GCancellable*, bool);
    ~Private();

    int attach(YubiKeyTag*);
    void detach();
    bool equals(Private*);
    bool setState(OpState);
    bool attachAndSetState(YubiKeyTag*, OpState);
    void submit(Operation*, YubiKeyTag*, bool aToFront);
    void selectOk(Operation*, const GUtilData*);
    bool start(Operation*);
    void done(Operation*);
    void cancelled(Operation*);
    void failed(Operation*);

    static void staticUnref(gpointer);
    static void staticCancel(GCancellable*, Operation*);
    static void staticLockResp(NfcTagClient*, NfcTagClientLock*, const GError*, void*);
    static void selectResp(NfcIsoDepClient*, const GUtilData*, guint aSw, const GError*, void*);
    static void selectResp2(NfcIsoDepClient*, const GUtilData*, guint aSw, const GError*, void*);
    static gboolean releaseLockLater(gpointer aLock);

public:
    static const uchar CMD_SELECT_DATA[];
    static const NfcIsoDepApdu CMD_SELECT;

public:
    QAtomicInt iRef;
    const char* iName;
    OpState iState;
    Operation* iNext;
    YubiKeyTag* iTag;
    NfcTagClientLock* iLock;
    NfcIsoDepClient* iIsoDep;
    GCancellable* iCancel;
    gulong iCancelId;
    int iId;
    bool iSuccess;
    const bool iRequireSelect;
};

#if HARBOUR_DEBUG
QDebug
operator<<(
    QDebug aDebug,
    YubiKeyTag::Operation::Private::OpState aState)
{
    switch (aState) {
#define PRINT_STATE_(State) \
    case YubiKeyTag::Operation::Private::Op##State: \
    return (aDebug << #State);
    OP_STATES(PRINT_STATE_)
#undef PRINT_STATE_
    }
    return (aDebug << (int)aState);
}
#endif // HARBOUR_DEBUG

// ==========================================================================
// YubiKeyTag::Operation::Private::Queue
// ==========================================================================

bool
YubiKeyTag::Operation::Private::Queue::isEmpty() const
{
    return !iFirst;
}

YubiKeyTag::Operation*
YubiKeyTag::Operation::Private::Queue::findEqual(
    Operation* aOperation)
{
    Operation* op = iFirst;

    while (op) {
        if (op->iPrivate->equals(aOperation->iPrivate)) {
            return op;
        }
        op = op->iPrivate->iNext;
    }
    return Q_NULLPTR;
}

void
YubiKeyTag::Operation::Private::Queue::pushFront(
    Operation* aOperation)
{
    HASSERT(aOperation);
    HASSERT(!aOperation->iPrivate->iNext);
    if (!iLast) {
        iLast = aOperation;
    }
    aOperation->iPrivate->iNext = iFirst;
    iFirst = aOperation->ref();
}

void
YubiKeyTag::Operation::Private::Queue::push(
    Operation* aOperation)
{
    HASSERT(aOperation);
    HASSERT(!aOperation->iPrivate->iNext);
    if (iLast) {
        HASSERT(!iLast->iPrivate->iNext);
        iLast->iPrivate->iNext = aOperation;
    } else {
        iFirst = aOperation;
    }
    iLast = aOperation->ref();
}

void
YubiKeyTag::Operation::Private::Queue::remove(
    Operation* aOperation)
{
    if (aOperation == iFirst) {
        take()->unref();
    } else if (iFirst) {
        Operation* op = iFirst;

        while (op->iPrivate->iNext) {
            if (op->iPrivate->iNext == aOperation) {
                if (!(op->iPrivate->iNext = aOperation->iPrivate->iNext)) {
                    iLast = op;
                }
                aOperation->unref();
                break;
            }
            op = op->iPrivate->iNext;
        }
    }
}

YubiKeyTag::Operation*
YubiKeyTag::Operation::Private::Queue::take()
{
    Operation* op = iFirst;

    if (op) {
        iFirst = op->iPrivate->iNext;
        op->iPrivate->iNext = Q_NULLPTR;
        if (!iFirst) {
            iLast = Q_NULLPTR;
        }
    }
    return op;
}

void
YubiKeyTag::Operation::Private::Queue::clear()
{
    while (iFirst) {
        Operation* op = iFirst;

        if (!(iFirst = op->iPrivate->iNext)) {
            iLast = Q_NULLPTR;
        }
        op->iPrivate->detach();
        op->cancel();
        op->unref();
    }
}

// ==========================================================================
// YubiKeyTag::Initialize declaration
// ==========================================================================

class YubiKeyTag::Initialize :
    public Operation {
public:
    Initialize(GCancellable*);
    bool startOperation() Q_DECL_OVERRIDE;
};

// ==========================================================================
// YubiKeyTag::Private
// ==========================================================================

class YubiKeyTag::Private :
    public YubiKeyConstants
{
    friend class Operation;
    typedef void (YubiKeyTag::*SignalEmitter)();
    typedef uint SignalMask;

    enum Signal {
#define SIGNAL_ENUM_(Name,name) Signal##Name##Changed,
        YUBIKEY_TAG_SIGNALS(SIGNAL_ENUM_)
#undef  SIGNAL_ENUM_
        SignalCount
    };

public:
    static QMap<QString,YubiKeyTag*> gMap;
    static int gLastOperationId;

    Private(const QString, YubiKeyTag*);
    ~Private();

    void queueSignal(Signal);
    void emitQueuedSignals();
    void setState(TagState);
    void setFinalState(TagState);
    void updateTagState();
    void updateIsoDepState();
    void updateYubiKeyId(const QByteArray);
    void updateYubiKeyVersion(const QByteArray);
    void updateYubiKeyAuthChallenge(const QByteArray);
    void updateYubiKeyAuthAlgorithm(YubiKeyAlgorithm);

    void ref();
    void unref();
    void dropObjects();
    bool transmit(YubiKeyTag*, const NfcIsoDepApdu*, QObject*, const char*, GCancellable*);

    static void tagEvent(NfcTagClient*, NFC_TAG_PROPERTY, void*);
    static void isoDepEvent(NfcIsoDepClient*, NFC_ISODEP_PROPERTY, void*);

public:
    const QString iPath;
    YubiKeyTag* iObject;
    QAtomicInt iRef;
    SignalMask iQueuedSignals;
    Signal iFirstQueuedSignal;
    QMap<int,bool> iFinishedOps;
    QByteArray iYubiKeyId;
    QByteArray iYubiKeyVersion;
    QByteArray iYubiKeyAuthChallenge;
    YubiKeyAlgorithm iYubiKeyAuthAlgorithm;
    QString iYubiKeyIdString;
    QString iYubiKeyVersionString;
    NfcTagClient* iTag;
    NfcIsoDepClient* iIsoDep;
    GCancellable* iCancel;
    gulong iTagEventId;
    gulong iIsoDepEventId;
    TagState iState;
    Operation* iActiveOp;
    Operation::Private::Queue iOpQueue;
    QList<int> iOperationIds;
};

QMap<QString,YubiKeyTag*> YubiKeyTag::Private::gMap;
int YubiKeyTag::Private::gLastOperationId;

YubiKeyTag::Private::Private(
    const QString aPath,
    YubiKeyTag* aObject) :
    iPath(aPath),
    iObject(aObject),
    iRef(1),
    iQueuedSignals(0),
    iFirstQueuedSignal(SignalCount),
    iYubiKeyAuthAlgorithm(YubiKeyAlgorithm_Unknown),
    iIsoDep(Q_NULLPTR),
    iCancel(Q_NULLPTR),
    iIsoDepEventId(0),
    iState(TagNone),
    iActiveOp(Q_NULLPTR)
{
    const QByteArray path(aPath.toLatin1());

    iTag = nfc_tag_client_new(path.constData());
    iTagEventId = nfc_tag_client_add_property_handler(iTag,
        NFC_TAG_PROPERTY_ANY, tagEvent, this);
}

YubiKeyTag::Private::~Private()
{
    dropObjects();
}

void
YubiKeyTag::Private::queueSignal(
    Signal aSignal)
{
    if (aSignal >= 0 && aSignal < SignalCount) {
        const SignalMask signalBit = (SignalMask(1) << aSignal);
        if (iQueuedSignals) {
            iQueuedSignals |= signalBit;
            if (iFirstQueuedSignal > aSignal) {
                iFirstQueuedSignal = aSignal;
            }
        } else {
            iQueuedSignals = signalBit;
            iFirstQueuedSignal = aSignal;
        }
    }
}

void
YubiKeyTag::Private::emitQueuedSignals()
{
    static const char* signalName [] = {
#define SIGNAL_NAME_(Name,name) G_STRINGIFY(name##Changed),
        YUBIKEY_TAG_SIGNALS(SIGNAL_NAME_)
#undef  SIGNAL_NAME_
    };
    Q_STATIC_ASSERT(G_N_ELEMENTS(signalName) == SignalCount);

    if (!iFinishedOps.isEmpty()) {
        const QMap<int,bool> ops(iFinishedOps);
        QMapIterator<int,bool> it(ops);

        iFinishedOps.clear();
        while (it.hasNext()) {
            it.next();
            HDEBUG(iObject << "operationFinished" << it.key() << it.value());
            // See https://bugreports.qt.io/browse/QTBUG-18434
            QMetaObject::invokeMethod(iObject, "operationFinished",
                Q_ARG(int, it.key()), Q_ARG(bool, it.value()));
        }
    }

    if (iQueuedSignals) {
        // Reset first queued signal before emitting the signals.
        // Signal handlers may emit more signals.
        uint i = iFirstQueuedSignal;
        iFirstQueuedSignal = SignalCount;
        for (; i < SignalCount && iQueuedSignals; i++) {
            const SignalMask signalBit = (SignalMask(1) << i);
            if (iQueuedSignals & signalBit) {
                iQueuedSignals &= ~signalBit;
                HDEBUG(iObject << signalName[i]);
                // See https://bugreports.qt.io/browse/QTBUG-18434
                QMetaObject::invokeMethod(iObject, signalName[i]);
            }
        }
    }
}

inline
void
YubiKeyTag::Private::ref()
{
    iRef.ref();
}

inline
void
YubiKeyTag::Private::unref()
{
    if (!iRef.deref()) {
        delete iObject;
    }
}

void
YubiKeyTag::Private::dropObjects()
{
    // Detach the ops first
    QList<Operation*> ops;

    if (iActiveOp) {
        iActiveOp->iPrivate->detach();
        ops.append(iActiveOp);
        iActiveOp = Q_NULLPTR;
    }

    Operation* op = iOpQueue.take();
    while (op) {
        op->iPrivate->detach();
        ops.append(op);
        op = iOpQueue.take();
    }

    // Cancel the detached ops
    while (!ops.isEmpty()) {
        op = ops.takeLast();
        op->cancel();
        op->unref();
    }

    // Then drop all other objects
    if (iCancel) {
        g_cancellable_cancel(iCancel);
        g_object_unref(iCancel);
        iCancel = Q_NULLPTR;
    }
    if (iIsoDep) {
        nfc_isodep_client_remove_handler(iIsoDep, iIsoDepEventId);
        nfc_isodep_client_unref(iIsoDep);
        iIsoDepEventId = 0;
        iIsoDep = Q_NULLPTR;
    }
    if (iTag) {
        nfc_tag_client_remove_handler(iTag, iTagEventId);
        nfc_tag_client_unref(iTag);
        iTagEventId = 0;
        iTag = Q_NULLPTR;
    }
}

void
YubiKeyTag::Private::setState(
    TagState aState)
{
    if (iState != aState) {
        HDEBUG(iState << "=>" << aState);
        iState = aState;
        queueSignal(SignalTagStateChanged);
    }
}

void
YubiKeyTag::Private::setFinalState(
    TagState aState)
{
    if (!iYubiKeyVersion.isEmpty()) {
        iYubiKeyVersion.clear();
        iYubiKeyVersionString.clear();
        queueSignal(SignalYubiKeyVersionChanged);
    }
    if (!iYubiKeyId.isEmpty()) {
        iYubiKeyId.clear();
        iYubiKeyIdString.clear();
        queueSignal(SignalYubiKeyIdChanged);
    }
    if (!iYubiKeyAuthChallenge.isEmpty()) {
        iYubiKeyAuthChallenge.clear();
        queueSignal(SignalYubiKeyAuthChallengeChanged);
    }
    if (iYubiKeyAuthAlgorithm != YubiKeyAlgorithm_Unknown) {
        iYubiKeyAuthAlgorithm = YubiKeyAlgorithm_Unknown;
        queueSignal(SignalYubiKeyAuthChallengeChanged);
    }
    dropObjects();
    setState(aState);
}

void
YubiKeyTag::Private::updateYubiKeyId(
    const QByteArray aId)
{
    if (iYubiKeyId != aId) {
        iYubiKeyId = aId;
        iYubiKeyIdString = YubiKeyUtil::toHex(aId);
        HDEBUG(qPrintable(iYubiKeyIdString));
        queueSignal(SignalYubiKeyIdChanged);
    }
}

void
YubiKeyTag::Private::updateYubiKeyVersion(
    const QByteArray aVersion)
{
    if (iYubiKeyVersion != aVersion) {
        iYubiKeyVersion = aVersion;
        iYubiKeyVersionString = YubiKeyUtil::versionToString(aVersion);
        HDEBUG(qPrintable(iYubiKeyVersionString));
        queueSignal(SignalYubiKeyVersionChanged);
    }
}

void
YubiKeyTag::Private::updateYubiKeyAuthChallenge(
    const QByteArray aChallenge)
{
    if (iYubiKeyAuthChallenge != aChallenge) {
        iYubiKeyAuthChallenge = aChallenge;
        HDEBUG(qPrintable(YubiKeyUtil::toHex(aChallenge)));
        queueSignal(SignalYubiKeyAuthChallengeChanged);
    }
}

void
YubiKeyTag::Private::updateYubiKeyAuthAlgorithm(
    YubiKeyAlgorithm aAlgorithm)
{
    if (iYubiKeyAuthAlgorithm != aAlgorithm) {
        iYubiKeyAuthAlgorithm = aAlgorithm;
        HDEBUG(aAlgorithm);
        queueSignal(SignalYubiKeyAuthAlgorithmChanged);
    }
}

void
YubiKeyTag::Private::tagEvent(
    NfcTagClient*,
    NFC_TAG_PROPERTY,
    void* aSelf)
{
    Private* self = (Private*)aSelf;

    self->ref();
    self->updateTagState();
    self->emitQueuedSignals();
    self->unref();
}

void
YubiKeyTag::Private::isoDepEvent(
    NfcIsoDepClient*,
    NFC_ISODEP_PROPERTY,
    void* aSelf)
{
    Private* self = (Private*)aSelf;

    self->updateIsoDepState();
    self->emitQueuedSignals();
}

void
YubiKeyTag::Private::updateTagState()
{
    if (iTag->valid) {
        if (iTag->present) {
            if (gutil_strv_contains(iTag->interfaces, NFC_TAG_INTERFACE_ISODEP)) {
                if (!iIsoDep) {
                    HDEBUG("ISO-DEP" << iTag->path);
                    iIsoDep = nfc_isodep_client_new(iTag->path);
                    iIsoDepEventId = nfc_isodep_client_add_property_handler(iIsoDep,
                        NFC_ISODEP_PROPERTY_ANY, isoDepEvent, this);
                    updateIsoDepState();
                }
            } else if (iState != TagUnrecognized) {
                // Not an ISO-DEP tag
                HDEBUG(iTag->path << "isn't ISO-DEP");
                setFinalState(TagUnrecognized);
            }
        } else {
            // The tag is gone and will never come back
            HDEBUG(iTag->path << "is gone");
            setFinalState(TagNone);
        }
    } else {
        setState(TagChecking);
    }
}

void
YubiKeyTag::Private::updateIsoDepState()
{
    if (iIsoDep->valid && iIsoDep->present && !iCancel) {
        // Start talking to the card
        iCancel = g_cancellable_new();
        HDEBUG("Checking" << iIsoDep->path);
        setState(TagChecking);
        Operation* init = new Initialize(iCancel);
        init->submit(iObject);
        init->unref();
    }
}

// ==========================================================================
// YubiKeyTag::Operation::Private
// ==========================================================================

const uchar YubiKeyTag::Operation::Private::CMD_SELECT_DATA[] = {
    0xa0, 0x00, 0x00, 0x05, 0x27, 0x21, 0x01
};

const NfcIsoDepApdu YubiKeyTag::Operation::Private::CMD_SELECT = {
    0x00, 0xa4, 0x04, 0x00, { CMD_SELECT_DATA, sizeof(CMD_SELECT_DATA) }, 0
};

YubiKeyTag::Operation::Private::Private(
    Operation* aOperation,
    const char* aName,
    GCancellable* aCancel,
    bool aRequireSelect) :
    iRef(1),
    iName(aName),
    iState(OpInit),
    iNext(Q_NULLPTR),
    iTag(Q_NULLPTR),
    iLock(Q_NULLPTR),
    iIsoDep(Q_NULLPTR),
    iCancel(aCancel ? ((GCancellable*) g_object_ref(aCancel)) : g_cancellable_new()),
    iCancelId(0),
    iId(0),
    iSuccess(false),
    iRequireSelect(aRequireSelect)
{
    if (!g_cancellable_is_cancelled(iCancel)) {
        iCancelId = g_cancellable_connect(iCancel, G_CALLBACK(staticCancel),
            aOperation, Q_NULLPTR);
    }
}

YubiKeyTag::Operation::Private::~Private()
{
    setState(OpDeleted);
    if (iCancelId) {
        g_signal_handler_disconnect(iCancel, iCancelId);
    }
    g_object_unref(iCancel);
    nfc_isodep_client_unref(iIsoDep);
    if (iLock) {
        // In case if another operation immediately follows this one,
        // keep it alive until the next event loop
        g_idle_add(releaseLockLater, iLock);
    }
}

int
YubiKeyTag::Operation::Private::attach(
    YubiKeyTag* aTag)
{
    YubiKeyTag::Private* tagPriv = aTag->iPrivate;

    HASSERT(!iTag);
    HASSERT(!iId);
    iTag = aTag;
    iId = qMax(YubiKeyTag::Private::gLastOperationId + 1, 1);
    while (tagPriv->iOperationIds.contains(iId)) {
        iId = qMax(iId + 1, 1);
    }

    YubiKeyTag::Private::gLastOperationId = iId;
    tagPriv->iOperationIds.append(iId);
    tagPriv->queueSignal(YubiKeyTag::Private::SignalOperationIdsChanged);
    qSort(tagPriv->iOperationIds);
    return iId;
}

void
YubiKeyTag::Operation::Private::detach()
{
    if (iTag) {
        YubiKeyTag::Private* tagPriv = iTag->iPrivate;

        HVERIFY(tagPriv->iOperationIds.removeOne(iId));
        HASSERT(!tagPriv->iOperationIds.contains(iId));
        tagPriv->queueSignal(YubiKeyTag::Private::SignalOperationIdsChanged);
        tagPriv->iFinishedOps.insert(iId, iSuccess);
        iTag = Q_NULLPTR;
    }
}

bool
YubiKeyTag::Operation::Private::attachAndSetState(
    YubiKeyTag* aTag,
    OpState aState)
{
    if (iState < aState) {
        // Mostly to make sure that setState always prints the id
        attach(aTag);
        HVERIFY(setState(aState));
        return true;
    }
    return false;
}

bool
YubiKeyTag::Operation::Private::setState(
    OpState aState)
{
    if (iState < aState) {
        HDEBUG(iName << iId << iState << "=>" << aState);
        iState = aState;
        return true;
    }
    return false;
}

bool
YubiKeyTag::Operation::Private::equals(
    Private* aOther)
{
    return iRequireSelect == aOther->iRequireSelect &&
        !strcmp(iName, aOther->iName);
}

void
YubiKeyTag::Operation::Private::submit(
    Operation* aOperation,
    YubiKeyTag* aTag,
    bool aToFront)
{
    // Caller checks the tag
    YubiKeyTag::Private* tagPriv = aTag->iPrivate;

    if (tagPriv->iActiveOp) {
        if (attachAndSetState(aTag, Private::OpQueued)) {
            if (aToFront) {
                tagPriv->iOpQueue.pushFront(aOperation);
            } else {
                tagPriv->iOpQueue.push(aOperation);
            }
        }
    } else if (attachAndSetState(aTag, Private::OpActive)) {
        tagPriv->iActiveOp = aOperation->ref();
        if (!start(aOperation)) {
            if (tagPriv->iActiveOp == aOperation) {
                tagPriv->iActiveOp = Q_NULLPTR;
                aOperation->unref();
            }
        }
    }
}

bool
YubiKeyTag::Operation::Private::start(
    Operation* aOperation)
{
    HASSERT(!iLock);
    HASSERT(!iIsoDep);

    if (iTag) {
        NfcIsoDepClient* isoDep = iTag->isoDep();

        if (isoDep && iCancelId) {
            iIsoDep = nfc_isodep_client_ref(isoDep);
            if (g_cancellable_is_cancelled(iCancel)) {
                // Already cancelled GCancellable was passed in
                cancelled(aOperation);
            } else {
                NfcTagClient* tag = nfc_isodep_client_tag(isoDep);

                iLock = nfc_tag_client_get_lock(tag);
                if (iLock) {
                    nfc_tag_client_lock_ref(iLock);
                    if (iRequireSelect) {
                        // Even if we already own the lock, we still
                        // need to re-SELECT the app
                        HDEBUG("SELECT");
                        if (nfc_isodep_client_transmit(isoDep, &CMD_SELECT,
                            iCancel, selectResp, aOperation, staticUnref)) {
                            DUMP_APDU(&CMD_SELECT);
                            iRef.ref();
                            return true;
                        }
                    } else {
                        // Since we own the lock, we can safely assume that
                        // the right application is already selected. The
                        // operation can be started right away
                        return aOperation->startOperation();
                    }
                } else if (nfc_tag_client_acquire_lock(tag, TRUE,
                    iCancel, staticLockResp, aOperation, staticUnref)) {
                    // Lock will acquired asynchronously
                    iRef.ref();
                    return true;
                }
            }
        }
    }
    return false;
}

void
YubiKeyTag::Operation::Private::done(
    Operation* aOperation)
{
    if (iTag) {
        YubiKeyTag* tag = iTag->ref();
        YubiKeyTag::Private* tagPriv = iTag->iPrivate;

        detach();
        if (tagPriv->iActiveOp == aOperation) {
            tagPriv->iActiveOp = Q_NULLPTR;
            aOperation->unref();

            // take() doesn't unref
            Operation* next = tagPriv->iOpQueue.take();

            while (next) {
                Private* nextPriv = next->iPrivate;

                HASSERT(nextPriv->iTag);
                if (nextPriv->setState(Private::OpActive)) {
                    tagPriv->iActiveOp = next;
                    if (nextPriv->start(next)) {
                        break;
                    }
                    if (tagPriv->iActiveOp == next) {
                        tagPriv->iActiveOp = Q_NULLPTR;
                        next->unref();
                    }
                }
                if (tagPriv->iActiveOp) {
                    break;
                } else {
                    next = tagPriv->iOpQueue.take();
                }
            }
        } else {
            tagPriv->iOpQueue.remove(aOperation);
        }
        tagPriv->emitQueuedSignals();
        tag->unref();
    }
}

void
YubiKeyTag::Operation::Private::cancelled(
    Operation* aOperation)
{
    if (iCancelId) {
        g_signal_handler_disconnect(iCancel, iCancelId);
        iCancelId = 0;
    }
    if (setState(OpCancelled)) {
        aOperation->operationCancelled();
    }
}

void
YubiKeyTag::Operation::Private::failed(
    Operation* aOperation)
{
    if (setState(OpFinished)) {
        aOperation->operationFailed();
    }
}

void
YubiKeyTag::Operation::Private::staticUnref(
    gpointer aSelf)
{
    ((Operation*)aSelf)->unref();
}

void
YubiKeyTag::Operation::Private::staticCancel(
    GCancellable*,
    Operation* aOperation)
{
    aOperation->iPrivate->cancelled(aOperation);
}

gboolean
YubiKeyTag::Operation::Private::releaseLockLater(
    gpointer aLock)
{
    nfc_tag_client_lock_unref((NfcTagClientLock*)aLock);
    return G_SOURCE_REMOVE;
}

void
YubiKeyTag::Operation::Private::staticLockResp(
    NfcTagClient*,
    NfcTagClientLock* aLock,
    const GError* aError,
    void* aOperation)
{
    Operation* self = (Operation*)aOperation;
    Private* priv = self->iPrivate;

    if (aLock) {
        priv->iLock = nfc_tag_client_lock_ref(aLock);
        HDEBUG("SELECT");
        if (nfc_isodep_client_transmit(priv->iIsoDep, &CMD_SELECT, priv->iCancel,
            selectResp, self, staticUnref)) {
            DUMP_APDU(&CMD_SELECT);
            self->ref();
            return;
        }
        // aError remains NULL
    }
    self->ref();
    self->lockFailed(aError);
    priv->failed(self);
    self->unref();
}

void
YubiKeyTag::Operation::Private::selectResp(
    NfcIsoDepClient*,
    const GUtilData* aResp,
    guint aSw,
    const GError* aError,
    void* aOperation)
{
    Operation* self = (Operation*)aOperation;
    Private* priv = self->iPrivate;

    self->ref();
    if (priv->iTag && !aError) {
        // 6883 appears to be recovertable and seems to be caused
        // by another app trying to select a non-existent app on
        // this YubiKey. The next attempt usually works.
        if (aSw == 0x6883) {
            HDEBUG("SELECT error" << hex << aSw << "(retrying)");
            if (nfc_isodep_client_transmit(priv->iIsoDep, &CMD_SELECT,
                priv->iCancel, selectResp2, self, staticUnref)) {
                DUMP_APDU(&CMD_SELECT);
                // The ref will be released by staticUnref
                return;
            }
        } else if (aSw == RC_OK && aResp) {
            priv->selectOk(self, aResp);
            self->unref();
            return;
        }
    }

    // Catch-all error case
    self->selectFailed(aSw, aError);
    priv->failed(self);
    self->unref();
}

void
YubiKeyTag::Operation::Private::selectResp2(
    NfcIsoDepClient*,
    const GUtilData* aResp,
    guint aSw,
    const GError* aError,
    void* aOperation)
{
    Operation* self = (Operation*)aOperation;
    Private* priv = self->iPrivate;

    self->ref();
    if (priv->iTag && !aError && aSw == RC_OK && aResp) {
        priv->selectOk(self, aResp);
    } else {
        self->selectFailed(aSw, aError);
        priv->failed(self);
    }
    self->unref();
}

void
YubiKeyTag::Operation::Private::selectOk(
    Operation* aOperation,
    const GUtilData* aResp)
{
    YubiKeyTag* tag = iTag;
    YubiKeyTag::Private* key = iTag->iPrivate;
    YubiKeyAlgorithm alg = YubiKeyAlgorithm_Unknown;
    QByteArray cardId, version, challenge;
    GUtilRange resp;
    GUtilData data;
    uchar t;

    HDEBUG("SELECT ok" << qPrintable(YubiKeyUtil::toHex(aResp)));
    resp.end = (resp.ptr = aResp->bytes) + aResp->size;
    while ((t = YubiKeyUtil::readTLV(&resp, &data)) != 0) {
        if (t == TLV_TAG_ALG) {
            if (data.size == 1) {
                alg = YubiKeyUtil::algorithmFromValue(data.bytes[0]);
                HDEBUG("Algorithm:" << alg);
            }
        } else {
            const QByteArray bytes((char*)data.bytes, data.size);
            switch (t) {
            case TLV_TAG_NAME:
                HDEBUG("Id:" << qPrintable(YubiKeyUtil::toHex(bytes)));
                cardId = bytes;
                break;
            case TLV_TAG_VERSION:
                HDEBUG("Version:" << qPrintable(YubiKeyUtil::versionToString(bytes)));
                version = bytes;
                break;
            case TLV_TAG_CHALLENGE:
                HDEBUG("Challenge:" << qPrintable(YubiKeyUtil::toHex(bytes)));
                challenge = bytes;
                break;
            default:
                HDEBUG("Unhandled tag" << hex << t);
                break;
            }
        }
    }

    iTag->ref();
    key->updateYubiKeyId(cardId);
    key->updateYubiKeyVersion(version);
    key->updateYubiKeyAuthChallenge(challenge);
    key->updateYubiKeyAuthAlgorithm(alg);
    key->emitQueuedSignals(); // This may cancel this operation
    if (iCancelId) {
        if (!aOperation->startOperation()) {
            aOperation->cancel();
        }
    }
    // Must use YubiKeyTag pointer from stack because the operation
    // may get detached from YubiKeyTag by now and iTag may be NULL
    tag->unref();
}

// ==========================================================================
// YubiKeyTag::Operation::Private::TransmitData
// ==========================================================================

YubiKeyTag::Operation::Private::TransmitData::TransmitData(
    Operation* aOperation,
    TransmitDone aCallback) :
    iOperation(aOperation->ref()),
    iCallback(aCallback)
{
}

YubiKeyTag::Operation::Private::TransmitData::~TransmitData()
{
    iOperation->unref();
}

void
YubiKeyTag::Operation::Private::TransmitData::staticResp(
    NfcIsoDepClient*,
    const GUtilData* aResp,
    guint aSw,
    const GError* aError,
    void* aData)
{
    TransmitData* data = (TransmitData*) aData;

    data->iCallback(data->iOperation, aResp, aSw, aError);
}

void
YubiKeyTag::Operation::Private::TransmitData::staticFree(
    gpointer aData)
{
    delete (TransmitData*) aData;
}

// ==========================================================================
// YubiKeyTag::Operation
// ==========================================================================

YubiKeyTag::Operation::Operation(
    const char* aName,
    GCancellable* aCancel,
    bool aRequireSelect) :
    iPrivate(new Private(this, aName, aCancel, aRequireSelect))
{
    HDEBUG(aName << ((void*)this));
}

YubiKeyTag::Operation::~Operation()
{
    delete iPrivate;
}

YubiKeyTag::Operation*
YubiKeyTag::Operation::ref()
{
    iPrivate->iRef.ref();
    return this;
}

void
YubiKeyTag::Operation::unref()
{
    if (!iPrivate->iRef.deref()) {
        delete this;
    }
}

int
YubiKeyTag::Operation::submit(
    YubiKeyTag* aTag,
    bool aToFront)
{
    if (aTag) {
        iPrivate->submit(this, aTag, aToFront);
        aTag->iPrivate->emitQueuedSignals();
        return iPrivate->iId;
    }
    return 0;
}

void
YubiKeyTag::Operation::submitUnique(
    YubiKeyTag* aTag,
    bool aToFront)
{
    if (aTag) {
        YubiKeyTag::Private* tagPriv = aTag->iPrivate;
        Operation* op = tagPriv->iOpQueue.findEqual(this);

        if (op) {
            op->iPrivate->detach();
            tagPriv->iOpQueue.remove(op);
        }
        iPrivate->submit(this, aTag, aToFront);
    }
}

void
YubiKeyTag::Operation::finished(
    bool aSuccess)
{
    if (iPrivate->setState(Private::OpFinished)) {
        iPrivate->iSuccess = aSuccess;
        iPrivate->done(this);
    }
}

void
YubiKeyTag::Operation::cancel()
{
    ref();
    g_cancellable_cancel(iPrivate->iCancel);
    HASSERT(!iPrivate->iCancelId);
    unref();
}

bool
YubiKeyTag::Operation::transmit(
    const NfcIsoDepApdu* aApdu)
{
    HASSERT(iPrivate->iIsoDep);
    if (nfc_isodep_client_transmit(iPrivate->iIsoDep, aApdu, iPrivate->iCancel,
        NULL, this, Private::staticUnref)) {
        DUMP_APDU(aApdu);
        ref();
        return true;
    }
    return false;
}

bool
YubiKeyTag::Operation::transmit(
    const NfcIsoDepApdu* aApdu,
    TransmitDone aMethod)
{
    Private::TransmitData* data = new Private::TransmitData(this, aMethod);

    HASSERT(iPrivate->iIsoDep);
    if (nfc_isodep_client_transmit(iPrivate->iIsoDep, aApdu,
        iPrivate->iCancel, Private::TransmitData::staticResp, data,
        Private::TransmitData::staticFree)) {
        DUMP_APDU(aApdu);
        return true;
    } else {
        delete data;
        return false;
    }
}

void
YubiKeyTag::Operation::lockFailed(
    const GError* aError)
{
    GERR("%s", GERRMSG(aError));
}

void
YubiKeyTag::Operation::selectFailed(
    guint aSw,
    const GError* aError)
{
    REPORT_ERROR("SELECT", aSw, aError);
}

void
YubiKeyTag::Operation::operationCancelled()
{
    iPrivate->done(this);
}

void
YubiKeyTag::Operation::operationFailed()
{
    iPrivate->done(this);
}

// ==========================================================================
// YubiKeyTag::Initialize
// ==========================================================================

YubiKeyTag::Initialize::Initialize(
    GCancellable* aCancel) :
    Operation("Initialize", aCancel, false)
{
}

bool
YubiKeyTag::Initialize::startOperation()
{
    YubiKeyTag::Private* priv = iPrivate->iTag->iPrivate;

    // Everything is already done by the base class
    priv->setState(TagYubiKeyReady);
    priv->emitQueuedSignals();
    finished();
    return true;
}

// ==========================================================================
// YubiKeyTag::Transmit
// ==========================================================================

class YubiKeyTag::Transmit :
    public YubiKeyTag::Operation
{
private:
    static const NfcIsoDepApdu CMD_SEND_REMAINING;

public:
    Transmit(const NfcIsoDepApdu*, QObject*, const char*, GCancellable*);

    bool startOperation() Q_DECL_OVERRIDE;
    void operationCancelled() Q_DECL_OVERRIDE;


private:
    ~Transmit();

    static void staticComplete(Operation*, const GUtilData*, guint, const GError*);
    void complete(const GUtilData*, guint, const GError*);

private:
    void* iData;
    QByteArray iRespBuf;
    NfcIsoDepApdu iApdu;
    QObject* iHandler;
    char* iMethod;
};

const NfcIsoDepApdu YubiKeyTag::Transmit::CMD_SEND_REMAINING = {
    0x00, 0xa5, 0x00, 0x00, { NULL, 0 }, 0
};

YubiKeyTag::Transmit::Transmit(
    const NfcIsoDepApdu* aApdu,
    QObject* aHandler,
    const char* aMethod,
    GCancellable* aCancel) :
    Operation("Transmit", aCancel, false),
    iData(gutil_memdup(aApdu->data.bytes, aApdu->data.size)),
    iApdu(*aApdu),
    iHandler(aHandler),
    iMethod(g_strdup(aMethod))
{
    iApdu.data.bytes = (const guint8*)iData;
}

YubiKeyTag::Transmit::~Transmit()
{
    complete(NULL, 0, NULL);
    g_free(iMethod);
    g_free(iData);
}

bool
YubiKeyTag::Transmit::startOperation()
{
    return transmit(&iApdu, &Transmit::staticComplete);
}

void
YubiKeyTag::Transmit::operationCancelled()
{
    g_free(iMethod);
    iMethod = Q_NULLPTR;
    iHandler = Q_NULLPTR;
    Operation::operationCancelled();
}

void
YubiKeyTag::Transmit::staticComplete(
    Operation* aSelf,
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    Transmit* self = (Transmit*)aSelf;
    QByteArray* respBuf = &self->iRespBuf;

    if (RC_MORE_DATA(aSw)) {
        respBuf->append((const char*)aResp->bytes, aResp->size);
        HDEBUG("CMD_SEND_REMAINING" << respBuf->size() << "+" <<
            (NFC_ISODEP_SW2(aSw) ? NFC_ISODEP_SW2(aSw) : 0x100));
        self->transmit(&CMD_SEND_REMAINING, &Transmit::staticComplete);
    } else if (respBuf->isEmpty()) {
        self->complete(aResp, aSw, aError);
    } else {
        GUtilData resp;

        respBuf->append((const char*)aResp->bytes, aResp->size);
        resp.bytes = (const guint8*)respBuf->constData();
        resp.size = respBuf->size();
        self->complete(&resp, aSw, aError);
    }
}

void
YubiKeyTag::Transmit::complete(
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    if (iHandler && iMethod)  {
        QObject* handler = iHandler;

        // The handler only gets invoked once
        iHandler = Q_NULLPTR;
        QMetaObject::invokeMethod(handler, iMethod,
            Q_ARG(const GUtilData*, aResp),
            Q_ARG(guint, aSw),
            Q_ARG(const GError*, aError));
    }
    finished(!aError);
}

// ==========================================================================
// YubiKeyTag
// ==========================================================================

YubiKeyTag::YubiKeyTag(
    const QString aPath) :
    iPrivate(new Private(aPath, this))
{
    HDEBUG(qPrintable(iPrivate->iPath));

    // Add is to the static map
    HASSERT(!Private::gMap.value(iPrivate->iPath));
    Private::gMap.insert(iPrivate->iPath, this);
}

YubiKeyTag::~YubiKeyTag()
{
    HDEBUG(qPrintable(iPrivate->iPath));

    // Remove it from the static map
    HASSERT(Private::gMap.value(iPrivate->iPath) == this);
    Private::gMap.remove(iPrivate->iPath);

    delete iPrivate;
}

YubiKeyTag*
YubiKeyTag::get(
    const QString aPath)
{
    YubiKeyTag* tag = Private::gMap.value(aPath);
    if (tag) {
        return tag->ref();
    } else {
        return new YubiKeyTag(aPath);
    }
}

YubiKeyTag*
YubiKeyTag::ref()
{
    iPrivate->ref();
    return this;
}

void
YubiKeyTag::unref()
{
    iPrivate->unref();
}

const QString
YubiKeyTag::path() const
{
    return iPrivate->iPath;
}

NfcIsoDepClient*
YubiKeyTag::isoDep() const
{
    return iPrivate->iIsoDep;
}

YubiKeyTag::TagState
YubiKeyTag::tagState() const
{
    return iPrivate->iState;
}

const QByteArray
YubiKeyTag::yubiKeyId() const
{
    return iPrivate->iYubiKeyId;
}

const QByteArray
YubiKeyTag::yubiKeyVersion() const
{
    return iPrivate->iYubiKeyVersion;
}

const QString
YubiKeyTag::yubiKeyIdString() const
{
    return iPrivate->iYubiKeyIdString;
}

const QString
YubiKeyTag::yubiKeyVersionString() const
{
    return iPrivate->iYubiKeyVersionString;
}

const QByteArray
YubiKeyTag::yubiKeyAuthChallenge() const
{
    return iPrivate->iYubiKeyAuthChallenge;
}

YubiKeyAlgorithm
YubiKeyTag::yubiKeyAuthAlgorithm() const
{
    return iPrivate->iYubiKeyAuthAlgorithm;
}

bool
YubiKeyTag::hasAuthChallenge() const
{
    return !iPrivate->iYubiKeyAuthChallenge.isEmpty();
}

const QList<int>
YubiKeyTag::operationIds() const
{
    return iPrivate->iOperationIds;
}

int
YubiKeyTag::transmit(
    const NfcIsoDepApdu* aApdu,
    QObject* aHandler,
    const char* aMethod,
    GCancellable* aCancel)
{
    NfcIsoDepClient* isoDep = iPrivate->iIsoDep;

    if (isoDep->valid && isoDep->present) {
        GCancellable* cancel = aCancel ? aCancel : iPrivate->iCancel;
        Transmit* tx = new Transmit(aApdu, aHandler, aMethod, cancel);
        int id = tx->submit(this);

        tx->unref();
        return id;
    }
    return 0;
}

bool
YubiKeyTag::deactivate()
{
    NfcTagClient* tag = iPrivate->iTag;

    return tag && tag->present && nfc_tag_client_deactivate(tag,
        iPrivate->iCancel, Q_NULLPTR, Q_NULLPTR, Q_NULLPTR);
}
