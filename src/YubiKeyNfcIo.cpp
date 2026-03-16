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

#include "nfcdc_error.h"
#include "nfcdc_isodep.h"
#include "nfcdc_tag.h"

#include "gutil_misc.h"
#include "gutil_strv.h"

#include "YubiKeyNfcIo.h"

#include "HarbourDebug.h"
#include "HarbourUtil.h"

// ==========================================================================
// YubiKeyNfcIo::Lock declaration
// ==========================================================================

class YubiKeyNfcIo::Lock :
    public IoLock
{
    void unlock();

public:
    Lock(Private*, NfcTagClientLock*);
    ~Lock() Q_DECL_OVERRIDE;

    void drop();

public:
    Private* iPrivate;
    NfcTagClientLock* iLock;
};

// ==========================================================================
// YubiKeyNfcIo::Private
// ==========================================================================

class YubiKeyNfcIo::Private :
    public QObject
{
    Q_OBJECT

public:
    Private(YubiKeyNfcIo*, NfcTagClient*);
    ~Private();

    static bool isYubiKeyHB(const GUtilData*);
    static void lockResponse(NfcTagClient*, NfcTagClientLock*, const GError*, void*);
    static void tagEvent(NfcTagClient*, NFC_TAG_PROPERTY, void*);
    static void isoDepValidEvent(NfcIsoDepClient*, NFC_ISODEP_PROPERTY, void*);

    void setState(IoState);
    void emitQueuedSignals();

    YubiKeyIo::Lock requestLock();

    void handleLockResponse(NfcTagClientLock*, const GError*);
    bool checkIsoDepHB();
    void updateTagState();
    bool updateSerial();

public:
    IoState iState;
    IoState iPrevState;
    uint iSerial;
    uint iPrevSerial;
    GCancellable* iCancel;
    NfcTagClient* iTag;
    gulong iTagEventId;
    NfcIsoDepClient* iIsoDep;
    gulong iIsoDepValidId;
    YubiKeyNfcIo::Lock* iLock;
    int iActiveTx;
};

YubiKeyNfcIo::Private::Private(
    YubiKeyNfcIo* aParent,
    NfcTagClient* aTag) :
    QObject(aParent),
    iState(IoUnknown),
    iPrevState(iState),
    iSerial(0),
    iPrevSerial(iSerial),
    iCancel(g_cancellable_new()),
    iTag(nfc_tag_client_ref(aTag)),
    iTagEventId(nfc_tag_client_add_property_handler(iTag,
        NFC_TAG_PROPERTY_ANY, tagEvent, this)),
    iIsoDep(Q_NULLPTR),
    iIsoDepValidId(0),
    iLock(Q_NULLPTR),
    iActiveTx(0)
{
    updateTagState();
}

YubiKeyNfcIo::Private::~Private()
{
    if (iLock) {
        iLock->iPrivate = Q_NULLPTR;
    }
    g_cancellable_cancel(iCancel);
    nfc_isodep_client_remove_handler(iIsoDep, iIsoDepValidId);
    nfc_isodep_client_unref(iIsoDep);
    nfc_tag_client_remove_handler(iTag, iTagEventId);
    nfc_tag_client_unref(iTag);
    g_object_unref(iCancel);
}

void
YubiKeyNfcIo::Private::emitQueuedSignals()
{
    if (iPrevState != iState || iPrevSerial != iSerial) {
        YubiKeyNfcIo* io = qobject_cast<YubiKeyNfcIo*>(parent());

        if (iPrevSerial != iSerial) {
            iPrevSerial = iSerial;
            Q_EMIT io->ioSerialChanged();
        }

        if (iPrevState != iState) {
            const IoState prev = iPrevState;

            iPrevState = iState;
            Q_EMIT io->ioStateChanged(prev);
        }
    }
}

void
YubiKeyNfcIo::Private::setState(
    IoState aState)
{
    // Never leave terminal states
    if (iState != aState && !isTerminalState(iState)) {
        HDEBUG(iTag->path << iState << "=>" << aState);
        switch (iState = aState) {
        case IoError:
        case IoTargetGone:
            // Stop receiving events from this tag
            nfc_tag_client_remove_handler(iTag, iTagEventId);
            iTagEventId = 0;
            // fallthrough
        case IoTargetInvalid:
            // Drop the lock if we had one
            if (iLock) {
                iLock->drop();
                iLock = Q_NULLPTR;
            }
            break;
        case IoUnknown:
        case IoReady:
        case IoLocking:
        case IoLocked:
        case IoActive:
            break;
        }
    }
}

YubiKeyIo::Lock
YubiKeyNfcIo::Private::requestLock()
{
    if (!iLock && !isTerminalState(iState)) {
        NfcTagClientLock* lock = nfc_tag_client_get_lock(iTag);
        if (lock) {
            iLock = new Lock(this, lock);
            setState(IoLocked);
        } else if (nfc_tag_client_acquire_lock(iTag, TRUE,
            iCancel, lockResponse, this, Q_NULLPTR)) {
            // Lock will be acquired asynchronously
            iLock = new Lock(this, Q_NULLPTR);
            if (iState != IoUnknown) {
                setState(IoLocking);
            }
        } else {
            setState(IoError);
        }
    }
    return YubiKeyIo::Lock(iLock);
}

/* static */
void
YubiKeyNfcIo::Private::tagEvent(
    NfcTagClient*,
    NFC_TAG_PROPERTY,
    void* aSelf)
{
    Private* self = (Private*)aSelf;

    self->updateTagState();
    self->emitQueuedSignals();
}

/* static */
void
YubiKeyNfcIo::Private::lockResponse(
    NfcTagClient*,
    NfcTagClientLock* aLock,
    const GError* aError,
    void* aSelf)
{
    Private* self = (Private*)aSelf;

    self->handleLockResponse(aLock, aError);
    self->emitQueuedSignals();
}

void
YubiKeyNfcIo::Private::handleLockResponse(
    NfcTagClientLock* aLock,
    const GError*)
{
    if (aLock) {
        // iLock may be null if the lock has been released before
        // it has actually been acquired
        if (iLock) {
            HASSERT(!iLock->iLock);
            iLock->iLock = nfc_tag_client_lock_ref(aLock);
            switch (iState) {
            case IoReady:
            case IoLocking:
                setState(IoLocked);
                break;
            case IoUnknown:
            case IoLocked:
            case IoActive:
            case IoTargetInvalid:
            case IoError:
            case IoTargetGone:
                break;
            }
        }
    } else {
        setState(IoError);
    }
}

void
YubiKeyNfcIo::Private::updateTagState()
{
    if (iTag->valid) {
        if (!iTag->present) {
            // The tag is gone and will never come back
            setState(IoTargetGone);
        } else if (iState == IoUnknown) {
            if (gutil_strv_contains(iTag->interfaces, NFC_TAG_INTERFACE_ISODEP)) {
                if (!iIsoDep) {
                    HDEBUG("ISO-DEP" << iTag->path);
                    if (updateSerial()) {
                        HDEBUG("Serial" << iSerial);
                    }
                    iIsoDep = nfc_isodep_client_new(iTag->path);
                    if (!checkIsoDepHB()) {
                        // The initial query hasn't completed yet
                        iIsoDepValidId = nfc_isodep_client_add_property_handler(iIsoDep,
                            NFC_ISODEP_PROPERTY_VALID, isoDepValidEvent, this);
                    }
                }
            } else if (iState != IoTargetInvalid) {
                // Not an ISO-DEP tag
                HDEBUG(iTag->path << "isn't ISO-DEP");
                setState(IoTargetInvalid);
            }
        }
    }
}

/* static */
void
YubiKeyNfcIo::Private::isoDepValidEvent(
    NfcIsoDepClient* aIsoDep,
    NFC_ISODEP_PROPERTY,
    void* aSelf)
{
    Private* self = (Private*)aSelf;

    if (self->checkIsoDepHB()) {
        // Don't need this event anymore
        nfc_isodep_client_remove_handler(aIsoDep, self->iIsoDepValidId);
        self->iIsoDepValidId = 0;
    }
    self->emitQueuedSignals();
}

bool
YubiKeyNfcIo::Private::checkIsoDepHB()
{
    if (iIsoDep->valid) {
        if (isYubiKeyHB(nfc_isodep_client_act_param(iIsoDep,
            NFC_ISODEP_ACT_PARAM_HB))) {
            setState(iLock ? IoLocked : IoReady);
        } else {
            HDEBUG(iTag->path << "doesn't look like YubiKey");
            setState(IoTargetInvalid);
        }
        return true;
    }
    return false;
}

bool
YubiKeyNfcIo::Private::isYubiKeyHB(
    const GUtilData* aHb)
{
    static const GUtilData yubiKeySuffix = { (const guint8*) "YubiKey", 7 };

    if (gutil_data_has_suffix(aHb, &yubiKeySuffix)) {
        HDEBUG("This is a YubiKey based on HB");
        return true;
    }
    return false;
}

bool
YubiKeyNfcIo::Private::updateSerial()
{
    const GUtilData* nfcid1 =  nfc_tag_client_poll_param(iTag,
        NFC_TAG_POLL_PARAM_NFCID1);

    if (nfcid1) {
        // https://docs.yubico.com/hardware/yubikey/yk-5/tech-manual/yk5-nfc-id-tech-desc.html
        // serial_0 is the most significant byte
        if (nfcid1->size == 7 && nfcid1->bytes[0] == 0x27) {
            if (nfcid1->bytes[1] == 0 && nfcid1->bytes[2] == 0) {
                // YubiKey 5.2.x and lower versions
                // 0x27 0 0 serial_3 serial_2 serial_1 serial_0
                iSerial = (uint)nfcid1->bytes[3] +
                    (((uint)nfcid1->bytes[4]) << 8) +
                    (((uint)nfcid1->bytes[5]) << 16) +
                    (((uint)nfcid1->bytes[6]) << 24);
                return true;
            } else if (nfcid1->bytes[1] == nfcid1->bytes[6] &&
                nfcid1->bytes[2] == nfcid1->bytes[5]) {
                // YubiKey v5.3.0 and Above
                // 0x27 serial_3 serial_2 serial_1 serial_0 serial_2 serial_3
                iSerial = (uint)nfcid1->bytes[1] +
                    (((uint)nfcid1->bytes[2]) << 8) +
                    (((uint)nfcid1->bytes[3]) << 16) +
                    (((uint)nfcid1->bytes[4]) << 24);
                return true;
            }
        }
    }
    return false;
}

// ==========================================================================
// YubiKeyNfcIo::Lock
// ==========================================================================

YubiKeyNfcIo::Lock::Lock(
    Private* aPrivate,
    NfcTagClientLock* aLock) :
    iPrivate(aPrivate),
    iLock(nfc_tag_client_lock_ref(aLock))
{}

YubiKeyNfcIo::Lock::~Lock()
{
    unlock();
    if (iPrivate) {
        HASSERT(iPrivate->iLock == this);
        iPrivate->iLock = Q_NULLPTR;
        switch (iPrivate->iState) {
        case IoLocking:
        case IoLocked:
            iPrivate->setState(IoReady);
            iPrivate->emitQueuedSignals();
            break;
        case IoUnknown:
        case IoReady:
        case IoActive:
        case IoTargetInvalid:
        case IoError:
        case IoTargetGone:
            break;
        }
    }
}

void
YubiKeyNfcIo::Lock::drop()
{
    unlock();
    iPrivate = Q_NULLPTR;
}

void
YubiKeyNfcIo::Lock::unlock()
{
    if (iLock) {
        nfc_tag_client_lock_unref(iLock);
        iLock = Q_NULLPTR;
    }
}

// ==========================================================================
// YubiKeyNfcIo::Tx
// ==========================================================================

class YubiKeyNfcIo::Tx :
    public YubiKeyIoTx
{
    Q_OBJECT

public:
    Tx(YubiKeyNfcIo*);
    ~Tx() Q_DECL_OVERRIDE;

    static void cancelHandler(GCancellable*, Tx*);
    static void responseHandler(NfcIsoDepClient*, const GUtilData*, guint, const GError*, void*);

    YubiKeyNfcIo* nfcIo() const;
    void activate();
    void deactivate(bool aMayDelete = true);
    void cancelled();
    void failed();
    void finished(Result, QByteArray);
    void emitQueuedIoSignals();

    // YubiKeyTx
    TxState txState() const  Q_DECL_OVERRIDE;
    void txSetAutoDelete(bool) Q_DECL_OVERRIDE;
    void txCancel() Q_DECL_OVERRIDE;

public:
    GCancellable* iCancel;
    gulong iCancelId;
    TxState iState;
    bool iActive;
    bool iAutoDelete;
};

YubiKeyNfcIo::Tx::Tx(
    YubiKeyNfcIo* aParent) :
    YubiKeyIoTx(aParent),
    iCancel(g_cancellable_new()),
    iCancelId(g_cancellable_connect(iCancel, G_CALLBACK(cancelHandler), this, NULL)),
    iState(TxPending),
    iActive(false),
    iAutoDelete(false)
{}

YubiKeyNfcIo::Tx::~Tx()
{
    deactivate(false);
    g_signal_handler_disconnect(iCancel, iCancelId);
    g_cancellable_cancel(iCancel);
    g_object_unref(iCancel);
}

YubiKeyIoTx::TxState
YubiKeyNfcIo::Tx::txState() const
{
    return iState;
}

void
YubiKeyNfcIo::Tx::txSetAutoDelete(
    bool aAutoDelete)
{
    if (iAutoDelete != aAutoDelete) {
        iAutoDelete = aAutoDelete;
        if (aAutoDelete && iState != TxPending) {
            HarbourUtil::scheduleDeleteLater(this);
        }
    }
}

void
YubiKeyNfcIo::Tx::txCancel()
{
    if (iState == TxPending) {
        g_cancellable_cancel(iCancel);
    }
}

inline
YubiKeyNfcIo*
YubiKeyNfcIo::Tx::nfcIo() const
{
    return qobject_cast<YubiKeyNfcIo*>(parent());
}

void
YubiKeyNfcIo::Tx::activate()
{
    if (!iActive) {
        YubiKeyNfcIo::Private* priv = nfcIo()->iPrivate;

        iActive = true;
        priv->iActiveTx++;
        priv->setState(IoActive);
    }
}

void
YubiKeyNfcIo::Tx::deactivate(
    bool aMayDelete)
{
    if (iActive) {
        YubiKeyNfcIo::Private* priv = nfcIo()->iPrivate;

        iActive = false;
        HASSERT(priv->iActiveTx > 0);
        priv->iActiveTx--;
        if (!priv->iActiveTx && priv->iState == IoActive) {
            priv->setState(priv->iLock ? IoLocked : IoReady);
        }
        if (aMayDelete && iAutoDelete) {
            HarbourUtil::scheduleDeleteLater(this);
        }
    }
}

void
YubiKeyNfcIo::Tx::cancelled()
{
    if (iState == TxPending) {
        iState = TxCancelled;
        Q_EMIT txCancelled();
    }
}

void
YubiKeyNfcIo::Tx::failed()
{
    if (iState == TxPending) {
        iState = TxFailed;
        Q_EMIT txFailed();
    }
}

void
YubiKeyNfcIo::Tx::finished(
    Result aCode,
    QByteArray aData)
{
    if (iState == TxPending) {
        iState = TxFinished;
        Q_EMIT txFinished(aCode, aData);
    }
}

inline
void
YubiKeyNfcIo::Tx::emitQueuedIoSignals()
{
    nfcIo()->iPrivate->emitQueuedSignals();
}

/* static */
void
YubiKeyNfcIo::Tx::cancelHandler(
    GCancellable*,
    Tx* aSelf)
{
    aSelf->cancelled();
    aSelf->deactivate();
    aSelf->emitQueuedIoSignals();
}

/* static */
void
YubiKeyNfcIo::Tx::responseHandler(
    NfcIsoDepClient*,
    const GUtilData* aResp,
    guint aSw,
    const GError* aError,
    void* aSelf)
{
    Tx* self = (Tx*) aSelf;

    if (aError) {
        self->failed();
    } else {
        Result code(aSw);
        QByteArray data;

        if (aResp && aResp->size) {
            data = QByteArray((char*) aResp->bytes, aResp->size);
            HDEBUG(data.toHex().constData() << code);
        } else {
            HDEBUG(code);
        }
        self->finished(code, data);
    }
    self->deactivate();
    self->emitQueuedIoSignals();
}

// ==========================================================================
// YubiKeyNfcIo
// ==========================================================================

YubiKeyNfcIo::YubiKeyNfcIo(
    NfcTagClient* aTag,
    QObject* aParent) :
    YubiKeyIo(aParent),
    iPrivate(new Private(this, aTag))
{}

YubiKeyNfcIo::~YubiKeyNfcIo()
{
    delete iPrivate;
}

const char*
YubiKeyNfcIo::ioPath() const
{
    return iPrivate->iTag->path;
}

YubiKeyIo::IoState
YubiKeyNfcIo::ioState() const
{
    return iPrivate->iState;
}

uint
YubiKeyNfcIo::ioSerial() const
{
    return iPrivate->iSerial;
}

YubiKeyIo::Lock
YubiKeyNfcIo::ioLock()
{
    YubiKeyIo::Lock lock(iPrivate->requestLock());

    iPrivate->emitQueuedSignals();
    return lock;
}

YubiKeyIoTx*
YubiKeyNfcIo::ioTransmit(
    const APDU& aApdu)
{
    if (iPrivate->iIsoDep) {
        Tx* tx = new Tx(this);
        NfcIsoDepApdu apdu;

        memset(&apdu, 0, sizeof(apdu));
        apdu.cla = aApdu.cla;
        apdu.ins = aApdu.ins;
        apdu.p1 = aApdu.p1;
        apdu.p2 = aApdu.p2;
        apdu.data.bytes = (guint8*) aApdu.data.constData();
        apdu.data.size = aApdu.data.size();
        apdu.le = aApdu.le;

        if (nfc_isodep_client_transmit(iPrivate->iIsoDep, &apdu,
            tx->iCancel, Tx::responseHandler, tx, Q_NULLPTR)) {
            HDEBUG(aApdu.name << hex << aApdu.cla << aApdu.ins <<
                aApdu.p1 << aApdu.p2 << aApdu.data.toHex().constData());
            tx->activate();
            return tx;
        }
        delete tx;
    }
    return Q_NULLPTR;
}

#include "YubiKeyNfcIo.moc"
