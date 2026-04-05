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

#include "YubiKeyUsbIo.h"

#include "HarbourDebug.h"
#include "HarbourUtil.h"

#include <QtCore/QAtomicInt>
#include <QtCore/QtEndian>
#include <QtCore/QPointer>

#include <libusb.h>

// CCID
// Specification for Integrated Circuit(s) Cards Interface Devices
// Revision 1.1
// April 22rd, 2005

#define CCID_PROTOCOL_TYPE_T0 (0x0001)
#define CCID_PROTOCOL_TYPE_T1 (0x0002)

struct CCID_ClassDescriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdCCID;
    uint8_t  bMaxSlotIndex;
    uint8_t  bVoltageSupport;
    uint32_t dwProtocols;
    uint32_t dwDefaultClock;
    uint32_t dwMaximumClock;
    uint8_t  bNumClockSupport;
    uint32_t dwDataRate;
    uint32_t dwMaxDataRate;
    uint8_t  bNumDataRatesSupported;
    uint32_t dwMaxIFSD;
    uint32_t dwSynchProtocols;
    uint32_t dwMechanical;
    uint32_t dwFeatures;
    uint32_t dwMaxCCIDMessageLength;
    uint8_t  bClassGetResponse;
    uint8_t  bclassEnvelope;
    uint16_t wLcdLayout;
    uint8_t  bPINSupport;
    uint8_t  bMaxCCIDBusySlots;
} Q_PACKED;

enum PC_to_RDR_MessageType {
    PC_to_RDR_Message_IccPowerOn = 0x62,
    PC_to_RDR_Message_IccPowerOff = 0x63,
    PC_to_RDR_Message_XfrBlock = 0x6f
};

struct PC_to_RDR_MsgHeader {
    uint8_t  bMessageType;
    uint32_t dwLength;
    uint8_t  bSlot;
    uint8_t  bSeq;
} Q_PACKED;

struct PC_to_RDR_IccPowerOn {
    PC_to_RDR_MsgHeader hdr;
    uint8_t  bPowerSelect;
    uint16_t abRFU;
} Q_PACKED;

struct PC_to_RDR_IccPowerOff {
    PC_to_RDR_MsgHeader hdr;
    uint8_t abRFU[3];
} Q_PACKED;

struct PC_to_RDR_XfrBlock {
    PC_to_RDR_MsgHeader hdr;
    uint8_t  bBWI;
    uint16_t wLevelParameter;
} Q_PACKED;

enum RDR_to_PC_MessageType {
    RDR_to_PC_Message_DataBlock = 0x80,
    RDR_to_PC_Message_SlotStatus = 0x81
};

#define CCID_COMMAND_STATUS_MASK 0xc0 // bStatus

enum RDR_to_PC_CommandStatus {
    RDR_to_PC_CommandOK = 0x00,
    RDR_to_PC_CommandFailed = 0x40,
    RDR_to_PC_CommandTimeExtension = 0x80
};

struct RDR_to_PC_MsgHeader {
    uint8_t  bMessageType;
    uint32_t dwLength;
    uint8_t  bSlot;
    uint8_t  bSeq;
    uint8_t  bStatus;
    uint8_t  bError;
} Q_PACKED;

struct RDR_to_PC_DataBlock {
    RDR_to_PC_MsgHeader hdr;
    uint8_t  bChainParameter;
} Q_PACKED;

struct RDR_to_PC_SlotStatus {
    RDR_to_PC_MsgHeader hdr;
    uint8_t  bClockStatus;
} Q_PACKED;

#define FROM_USB_ENDIAN(x) qFromLittleEndian(x)
#define TO_USB_ENDIAN(x) qToLittleEndian(x)

// ==========================================================================
// YubiKeyUsbIo::Handle declaration
// Ref-counted libusb_device_handle
// ==========================================================================

class YubiKeyUsbIo::Handle
{
public:
    Handle(const Context&, libusb_device*);
    Handle(const Handle&);
    ~Handle();

    Handle& operator = (const Handle&);
    operator struct libusb_device_handle*() const;

private:
    class Impl;
    Impl* iImpl;
};

// ==========================================================================
// YubiKeyUsbIo::Lock declaration
// ==========================================================================

class YubiKeyUsbIo::Lock :
    public IoLockData
{
    Lock(Private*);

    static void iccPowerOnRequestSent(libusb_transfer*);
    static void iccPowerOnResponseReceived(libusb_transfer*);
    static void iccPowerOffResponseReceived(libusb_transfer*);

public:
    ~Lock() Q_DECL_OVERRIDE;

    static Lock* create(Private*);
    void release();
    void drop();

public:
    Private* iPrivate;
    Handle iHandle;
    const int iIntfNum;
};

// ==========================================================================
// YubiKeyUsbIo::Handle
// ==========================================================================

class YubiKeyUsbIo::Handle::Impl
{
public:
    Impl(
        const Context& aContext,
        libusb_device* aDevice) :
        iRef(1),
        iContext(aContext),
        iDevice(libusb_ref_device(aDevice)),
        iHandle(Q_NULLPTR)
    {
        int rc = libusb_open(aDevice, &iHandle);

        if (rc != LIBUSB_SUCCESS) {
            HWARN("Failed to open USB device" <<
                  libusb_strerror((enum libusb_error)rc));
        }
    }

    ~Impl()
    {
        if (iHandle) {
            libusb_close(iHandle);
        }
        libusb_unref_device(iDevice);
    }

public:
    QAtomicInt iRef;
    Context iContext;
    libusb_device* iDevice;
    libusb_device_handle* iHandle;
};

YubiKeyUsbIo::Handle::Handle(
    const Context& aContext,
    libusb_device* aDevice) :
    iImpl(new Impl(aContext, aDevice))
{}

YubiKeyUsbIo::Handle::Handle(
    const Handle& aHandle) :
    iImpl(aHandle.iImpl)
{
    iImpl->iRef.ref();
}

YubiKeyUsbIo::Handle::~Handle()
{
    if (!iImpl->iRef.deref()) {
        delete iImpl;
    }
}

YubiKeyUsbIo::Handle&
YubiKeyUsbIo::Handle::operator=(
    const Handle& aHandle)
{
    if (iImpl != aHandle.iImpl) {
        if (!iImpl->iRef.deref()) {
            delete iImpl;
        }
        (iImpl = aHandle.iImpl)->iRef.ref();
    }
    return *this;
}

YubiKeyUsbIo::Handle::operator
struct libusb_device_handle*() const
{
    return iImpl->iHandle;
}

// ==========================================================================
// YubiKeyUsbIo::Context
// Ref-counted libusb_context
// ==========================================================================

class YubiKeyUsbIo::Context::Private
{
public:
    Private() :
        iRef(1),
        iContext(Q_NULLPTR)
    {
        int rc = libusb_init(&iContext);

        if (rc != LIBUSB_SUCCESS) {
            HWARN("Failed to initialize libusb" <<
                  libusb_strerror((enum libusb_error)rc));
        }
    }

    ~Private()
    {
        if (iContext) {
            libusb_exit(iContext);
        }
    }

public:
    QAtomicInt iRef;
    struct libusb_context* iContext;
};


YubiKeyUsbIo::Context::Context() :
    iPrivate(new Private)
{}

YubiKeyUsbIo::Context::Context(
    const Context& aContext) :
    iPrivate(aContext.iPrivate)
{
    iPrivate->iRef.ref();
}

YubiKeyUsbIo::Context::~Context()
{
    if (!iPrivate->iRef.deref()) {
        delete iPrivate;
    }
}

YubiKeyUsbIo::Context&
YubiKeyUsbIo::Context::operator=(
    const Context& aContext)
{
    if (iPrivate != aContext.iPrivate) {
        if (!iPrivate->iRef.deref()) {
            delete iPrivate;
        }
        (iPrivate = aContext.iPrivate)->iRef.ref();
    }
    return *this;
}

YubiKeyUsbIo::Context::operator
struct libusb_context*() const
{
    return iPrivate->iContext;
}

// ==========================================================================
// YubiKeyUsbIo::Private
// ==========================================================================

class YubiKeyUsbIo::Private :
    public QObject
{
    Q_OBJECT

public:
    enum { TIMEOUT_MS = 2000 };

    Private(YubiKeyUsbIo*, Context&, libusb_device*, IntfAlt);
    ~Private();

    void setState(IoState);
    void emitQueuedSignals();

    IoLock lock();
    void deactivate();

    template <typename T> static QByteArray byteArray(T*);
    template <typename T> static T* alloc0();
    static bool submitTransfer(libusb_transfer*);
    static void reqCompleted(libusb_transfer*);
    static void respCompleted(libusb_transfer*);

public:
    IoState iState, iPrevState;
    Handle iHandle;
    Lock* iLock;
    int iActiveTx;
    uchar iSeq;
    uint iMaxCCIDMessageLength;
    uchar iBulkInEp, iBulkOutEp;
    const IntfAlt iInterface;
    const QByteArray iPath;
};

YubiKeyUsbIo::Private::Private(
    YubiKeyUsbIo* aParent,
    Context& aContext,
    libusb_device* aDevice,
    IntfAlt aInterface) :
    QObject(aParent),
    iState(IoError),
    iHandle(aContext, aDevice),
    iLock(Q_NULLPTR),
    iActiveTx(0),
    iSeq(0),
    iMaxCCIDMessageLength(0),
    iBulkInEp(0),
    iBulkOutEp(0),
    iInterface(aInterface),
    iPath(QString("%1:%2").arg(libusb_get_bus_number(aDevice)).
                           arg(libusb_get_port_number(aDevice)).toUtf8())
{
    libusb_config_descriptor* config = Q_NULLPTR;

    if (iHandle &&
        libusb_get_config_descriptor(aDevice, 0, &config) == LIBUSB_SUCCESS) {
        const libusb_interface_descriptor* intf =
            config->interface[iInterface.iIntfNum].altsetting +
                iInterface.iAltSetting;
        const CCID_ClassDescriptor* ccid = (CCID_ClassDescriptor*) intf->extra;
        const uint protocols = FROM_USB_ENDIAN(ccid->dwProtocols);

        // Make sure that the expected protocol is supported
        if (protocols & CCID_PROTOCOL_TYPE_T1) {
            uchar bulkIn = 0, bulkOut = 0;
            uint i;

            // Figure out the endpoint numbers
            for (i = 0; i < intf->bNumEndpoints && !(bulkIn && bulkOut); i++) {
                const libusb_endpoint_descriptor* ep = intf->endpoint + i;

                if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
                    LIBUSB_TRANSFER_TYPE_BULK) {
                    if (ep->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                        bulkIn = ep->bEndpointAddress;
                        HDEBUG("Bulk-IN EP" << hex << bulkIn);
                    } else {
                        bulkOut = ep->bEndpointAddress;
                        HDEBUG("Bulk-OUT EP" << hex << bulkOut);
                    }
                }
            }

            if (bulkIn && bulkOut) {
                iBulkInEp = bulkIn;
                iBulkOutEp = bulkOut;
                iMaxCCIDMessageLength =
                    FROM_USB_ENDIAN(ccid->dwMaxCCIDMessageLength);
                HDEBUG("dwMaxCCIDMessageLength" << iMaxCCIDMessageLength);
                iState = IoReady;
            }
        }
        libusb_free_config_descriptor(config);
    }
    iPrevState = iState;
}

YubiKeyUsbIo::Private::~Private()
{
    if (iLock) {
        iLock->iPrivate = Q_NULLPTR;
    }
}

void
YubiKeyUsbIo::Private::emitQueuedSignals()
{
    if (iPrevState != iState) {
        const IoState prev = iPrevState;
        YubiKeyUsbIo* io = qobject_cast<YubiKeyUsbIo*>(parent());

        iPrevState = iState;
        Q_EMIT io->ioStateChanged(prev);
    }
}

void
YubiKeyUsbIo::Private::setState(
    IoState aState)
{
    // Never leave terminal states
    if (iState != aState && !isTerminalState(iState)) {
        HDEBUG(iPath.constData() << iState << "=>" << aState);
        switch (iState = aState) {
        case IoError:
        case IoTargetGone:
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

YubiKeyIo::IoLock
YubiKeyUsbIo::Private::lock()
{
    if (iHandle && !iLock) {
        if ((iLock = YubiKeyUsbIo::Lock::create(this)) != Q_NULLPTR) {
            setState(IoLocking);
        } else {
            setState(IoError);
        }
    }
    return IoLock(iLock);
}

void
YubiKeyUsbIo::Private::deactivate()
{
    HASSERT(iActiveTx > 0);
    iActiveTx--;
    if (!iActiveTx && iState == IoActive) {
        setState(iLock ? IoLocked : IoReady);
    }
}

/* static */
template <typename T>
QByteArray
YubiKeyUsbIo::Private::byteArray(
    T* t)
{
    return QByteArray((char*) t, sizeof(*t));
}

/* static */
template <typename T> T*
YubiKeyUsbIo::Private::alloc0()
{
    T* t = (T*) malloc(sizeof(T));

    memset(t, 0, sizeof(T));
    return t;
}

/* static */
bool
YubiKeyUsbIo::Private::submitTransfer(
    libusb_transfer* aTransfer)
{
    int r = libusb_submit_transfer(aTransfer);

    if (r == LIBUSB_SUCCESS) {
        return true;
    } else {
        // Cleanup
        HWARN("USB tx error" << r);
        aTransfer->status = (libusb_transfer_status) r;
        aTransfer->callback(aTransfer);
        return false;
    }
}

/* static */
void
YubiKeyUsbIo::Private::reqCompleted(
    libusb_transfer* aTransfer)
{
    PC_to_RDR_MsgHeader* msg = (PC_to_RDR_MsgHeader*) aTransfer->buffer;

    // Generic completion callback
#if HARBOUR_DEBUG
    if (aTransfer->status == LIBUSB_TRANSFER_COMPLETED) {
        HDEBUG("USB req" << msg->bSeq << "ok");
    } else {
        HDEBUG("USB req" << msg->bSeq << "status" << aTransfer->status);
    }
#endif
    free(msg);
    libusb_free_transfer(aTransfer);
}

/* static */
void
YubiKeyUsbIo::Private::respCompleted(
    libusb_transfer* aTransfer)
{
    RDR_to_PC_MsgHeader* hdr = (RDR_to_PC_MsgHeader*) aTransfer->buffer;

    // Generic completion callback
#if HARBOUR_DEBUG
    if (aTransfer->status == LIBUSB_TRANSFER_COMPLETED) {
        const uint len = aTransfer->actual_length;
        const QByteArray hex(QByteArray((char*) hdr, len).toHex());
        if (len >= sizeof(*hdr)) {
            HDEBUG("USB resp" << hdr->bSeq << "ok" << hex.constData());
        } else {
            HDEBUG("USB resp" << hex.constData());
        }
    } else {
        HDEBUG("USB resp status" << aTransfer->status);
    }
#endif

    free(hdr);
    libusb_free_transfer(aTransfer);
}

// ==========================================================================
// YubiKeyUsbIo::Lock
// ==========================================================================

YubiKeyUsbIo::Lock::Lock(
    Private* aPrivate) :
    iPrivate(aPrivate),
    iHandle(aPrivate->iHandle),
    iIntfNum(aPrivate->iInterface.iIntfNum)
{
    // The interface is already claimed, switch the ICC power on
    // The max response size is 33 bytes ATR + 10 bytes RDR_to_PC_DataBlock.
    // Let's make it 64 to give it some extra room.
    const uint rsize = 64;
    libusb_transfer* resp = libusb_alloc_transfer(0);
    libusb_fill_bulk_transfer(resp, iHandle, aPrivate->iBulkInEp,
        (uchar*) malloc(rsize), rsize, iccPowerOnResponseReceived,
        new QPointer<Private>(aPrivate), Private::TIMEOUT_MS);

    if (Private::submitTransfer(resp)) {
        PC_to_RDR_IccPowerOn* msg = Private::alloc0<PC_to_RDR_IccPowerOn>();

        // YubiKeyUsbIo::Private may be deallocated before completion
        // of the USB transfer, hence QPointer<Private>
        msg->hdr.bMessageType = PC_to_RDR_Message_IccPowerOn;
        msg->hdr.bSeq = iPrivate->iSeq++;
        libusb_transfer* req = libusb_alloc_transfer(0);
        libusb_fill_bulk_transfer(req, iHandle, aPrivate->iBulkOutEp,
            (uchar*) msg, sizeof(*msg), iccPowerOnRequestSent,
            new QPointer<Private>(aPrivate), Private::TIMEOUT_MS);

        if (Private::submitTransfer(req)) {
            HDEBUG("IccPowerOn" << Private::byteArray(msg).toHex().constData());
        }
    }
}

YubiKeyUsbIo::Lock::~Lock()
{
    release();
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
YubiKeyUsbIo::Lock::drop()
{
    release();
    iPrivate = Q_NULLPTR;
}

void
YubiKeyUsbIo::Lock::release()
{
    if (iPrivate) {
        // Power off the ICC before releasing the interface.
        // YubiKeyUsbIo::Private may be deallocated before completion
        // of the USB transfer, hence QPointer<Private>
        libusb_transfer* resp = libusb_alloc_transfer(0);
        libusb_fill_bulk_transfer(resp, iHandle, iPrivate->iBulkInEp,
            (uchar*) Private::alloc0<RDR_to_PC_SlotStatus>(),
            sizeof(RDR_to_PC_SlotStatus), iccPowerOffResponseReceived,
            new QPointer<Private>(iPrivate), Private::TIMEOUT_MS);

        if (Private::submitTransfer(resp)) {
            libusb_transfer* req = libusb_alloc_transfer(0);
            PC_to_RDR_IccPowerOff* msg = Private::alloc0<PC_to_RDR_IccPowerOff>();

            msg->hdr.bMessageType = PC_to_RDR_Message_IccPowerOff;
            msg->hdr.bSeq = iPrivate->iSeq++;
            HDEBUG("IccPowerOff" << Private::byteArray(msg).toHex().constData());
            libusb_fill_bulk_transfer(req, iHandle, iPrivate->iBulkOutEp,
                (uchar*) msg, sizeof(*msg), Private::reqCompleted,
                Q_NULLPTR, Private::TIMEOUT_MS);
            Private::submitTransfer(req);
        }

        iPrivate->iLock = Q_NULLPTR;
        iPrivate = Q_NULLPTR;
    } else {
        libusb_release_interface(iHandle, iIntfNum);
    }
}

/* static */
YubiKeyUsbIo::Lock*
YubiKeyUsbIo::Lock::create(
    Private* aPrivate)
{
    libusb_device_handle* handle = aPrivate->iHandle;

    if (handle) {
        const IntfAlt* intf = &aPrivate->iInterface;

        if (libusb_claim_interface(handle, intf->iIntfNum) ==
            LIBUSB_SUCCESS) {
            if (libusb_set_interface_alt_setting(handle, intf->iIntfNum,
                intf->iAltSetting) == LIBUSB_SUCCESS) {
                return new YubiKeyUsbIo::Lock(aPrivate);
            }
            libusb_release_interface(handle, intf->iIntfNum);
            HWARN("Failed to set interface" << intf->iIntfNum <<
                intf->iAltSetting);
        } else {
            HWARN("Failed to claim interface" << intf->iIntfNum);
        }
    }
    return Q_NULLPTR;
}

/* static */
void
YubiKeyUsbIo::Lock::iccPowerOnRequestSent(
    libusb_transfer* aTransfer)
{
    QPointer<Private>* ptr = (QPointer<Private>*) aTransfer->user_data;
    PC_to_RDR_MsgHeader* msg = (PC_to_RDR_MsgHeader*) aTransfer->buffer;

    if (aTransfer->status == LIBUSB_TRANSFER_COMPLETED) {
        HDEBUG("IccPowerOn" << msg->bSeq << "ok");
    } else {
        Private* priv = ptr->data();

        HWARN("IccPowerOn" << msg->bSeq << "status" << aTransfer->status);
        if (priv) {
            priv->iLock = Q_NULLPTR;
            priv->setState(IoError);
            priv->emitQueuedSignals();
        }
    }

    delete ptr;
    free(msg);
    libusb_free_transfer(aTransfer);
}

/* static */
void
YubiKeyUsbIo::Lock::iccPowerOnResponseReceived(
    libusb_transfer* aTransfer)
{
    QPointer<Private>* ptr = (QPointer<Private>*) aTransfer->user_data;
    PC_to_RDR_MsgHeader* msg = (PC_to_RDR_MsgHeader*) aTransfer->buffer;
    const uint len = aTransfer->actual_length;
    Private* priv = ptr->data();

    if (aTransfer->status == LIBUSB_TRANSFER_COMPLETED &&
        len >= sizeof(*msg) &&
        msg->bMessageType == RDR_to_PC_Message_DataBlock &&
        FROM_USB_ENDIAN(msg->dwLength) == len - sizeof(RDR_to_PC_DataBlock)) {
        HDEBUG("ATR" << QByteArray((char*)(((RDR_to_PC_DataBlock*) msg) + 1),
            FROM_USB_ENDIAN(msg->dwLength)).toHex().constData());
        if (priv) {
            priv->setState(IoLocked);
            priv->emitQueuedSignals();
        }
    } else {
        HWARN("Unexpected ICC power on resp" << QByteArray((char*) msg, len).
            toHex().constData());
        if (priv) {
            priv->iLock = Q_NULLPTR;
            priv->setState(IoError);
            priv->emitQueuedSignals();
        }
    }

    delete ptr;
    free(msg);
    libusb_free_transfer(aTransfer);
}

/* static */
void
YubiKeyUsbIo::Lock::iccPowerOffResponseReceived(
    libusb_transfer* aTransfer)
{
    QPointer<Private>* ptr = (QPointer<Private>*) aTransfer->user_data;
    PC_to_RDR_MsgHeader* msg = (PC_to_RDR_MsgHeader*) aTransfer->buffer;

    if (aTransfer->status == LIBUSB_TRANSFER_COMPLETED) {
        Private* priv = ptr->data();

        HDEBUG("ICC power off resp" << QByteArray((char*) msg,
            aTransfer->actual_length).toHex().constData());
        if (priv && priv->iHandle) {
            libusb_release_interface(priv->iHandle, priv->iInterface.iIntfNum);
        }
    } else {
        HWARN("ICC power off" << msg->bSeq << "status" << aTransfer->status);
    }

    delete ptr;
    free(msg);
    libusb_free_transfer(aTransfer);
}

// ==========================================================================
// YubiKeyUsbIo::Tx
// ==========================================================================

class YubiKeyUsbIo::Tx :
    public YubiKeyIoTx
{
public:
    Tx(YubiKeyUsbIo*, const APDU&);
    ~Tx() Q_DECL_OVERRIDE;

    static void dataSent(libusb_transfer*);
    static void dataReceived(libusb_transfer*);
    static QByteArray encodeApdu(const APDU&);

    YubiKeyUsbIo* usbIo() const;
    void deactivate();
    void failed();
    void finished(Result, QByteArray);

    // YubiKeyTx
    TxState txState() const  Q_DECL_OVERRIDE;
    void txSetAutoDelete(bool) Q_DECL_OVERRIDE;
    void txCancel() Q_DECL_OVERRIDE;

public:
    TxState iState;
    bool iActive;
    bool iAutoDelete;
    const uchar iSeq;
};

YubiKeyUsbIo::Tx::Tx(
    YubiKeyUsbIo* aUsb,
    const APDU& aApdu) :
    YubiKeyIoTx(aUsb),
    iState(TxFailed),
    iActive(false),
    iAutoDelete(false),
    iSeq(aUsb->iPrivate->iSeq++)
{
    Private* priv = aUsb->iPrivate;
    libusb_transfer* resp = libusb_alloc_transfer(0);

    // Tx may be deallocated before completion of the USB transfer,
    // hence QPointer<Tx>
    libusb_fill_bulk_transfer(resp, priv->iHandle, priv->iBulkInEp,
        (uchar*) malloc(priv->iMaxCCIDMessageLength),
        priv->iMaxCCIDMessageLength, dataReceived, new QPointer<Tx>(this),
        Private::TIMEOUT_MS);
    if (Private::submitTransfer(resp)) {
        libusb_transfer* req = libusb_alloc_transfer(0);
        const QByteArray apdu(encodeApdu(aApdu));
        const uint xfrSize = sizeof(PC_to_RDR_XfrBlock) + apdu.size();
        PC_to_RDR_XfrBlock* xfr = (PC_to_RDR_XfrBlock*) malloc(xfrSize);

        memset(xfr, 0, sizeof(*xfr));
        memcpy(xfr + 1, apdu.constData(), apdu.size());
        xfr->hdr.bMessageType = PC_to_RDR_Message_XfrBlock;
        xfr->hdr.dwLength = TO_USB_ENDIAN((uint32_t) apdu.size());
        xfr->hdr.bSeq = iSeq;
        libusb_fill_bulk_transfer(req, priv->iHandle, priv->iBulkOutEp,
            (uchar*) xfr, xfrSize, dataSent, new QPointer<Tx>(this),
            Private::TIMEOUT_MS);
        if (Private::submitTransfer(req)) {
            HDEBUG(aApdu.name << apdu.toHex().constData());
            HDEBUG("USB xfr" << iSeq);
            iState = TxPending;
            iActive = true;
            priv->iActiveTx++;
        }
    }
}

YubiKeyUsbIo::Tx::~Tx()
{
    if (iActive) {
        // If the parent is being deleted, usbIo() return NULL
        YubiKeyUsbIo* io = usbIo();

        if (io) {
            YubiKeyUsbIo::Private* priv = io->iPrivate;

            iActive = false;
            priv->deactivate();
            priv->emitQueuedSignals();
        }
    }
}

YubiKeyIoTx::TxState
YubiKeyUsbIo::Tx::txState() const
{
    return iState;
}

void
YubiKeyUsbIo::Tx::txCancel()
{
    // CCID Abort functionality doesn't seem to be supported by Yubikeys :(
    // At least by firmware versions up to and including 5.7.4

    // Control pipe ABORT followed by PC_to_RDR_Abort over the Bulk-OUT pipe
    // results in a RDR_to_PC_SlotStatus Bulk-IN response like this:
    //
    // 8100000000000A400000 i.e.
    //
    //   bMessageType 81h        RDR_to_PC_SlotStatus
    //   dwLength     00000000h
    //   bSlot        00h
    //   bSeq         0Ah
    //   bStatus      40h        Failed
    //   bError       00h        Command not supported
    //   bClockStatus 00h
    //
    // Sounds like it's trying to tell us that the CCID Abort request is not
    // supported.

    // This is especially painful when HOTP code is being refreshed. The
    // CALCULATE transaction may remain pending for up to 30 sec (if no one
    // touches the key) and there seems to be no way to cancel that. And
    // until its completion, any other request fails with XFR_OVERRUN error.
}

void
YubiKeyUsbIo::Tx::txSetAutoDelete(
    bool aAutoDelete)
{
    if (iAutoDelete != aAutoDelete) {
        iAutoDelete = aAutoDelete;
        if (aAutoDelete && iState != TxPending) {
            HarbourUtil::scheduleDeleteLater(this);
        }
    }
}

/* static */
QByteArray
YubiKeyUsbIo::Tx::encodeApdu(
    const APDU& aApdu)
{
    // Command APDU encoding options (ISO/IEC 7816-4):
    //
    // Case 1:  |CLA|INS|P1|P2|                                n = 4
    // Case 2s: |CLA|INS|P1|P2|LE|                             n = 5
    // Case 3s: |CLA|INS|P1|P2|LC|...BODY...|                  n = 6..260
    // Case 4s: |CLA|INS|P1|P2|LC|...BODY...|LE|               n = 7..261
    // Case 2e: |CLA|INS|P1|P2|00|LE1|LE2|                     n = 7
    // Case 3e: |CLA|INS|P1|P2|00|LC1|LC2|...BODY...|          n = 8..65542
    // Case 4e: |CLA|INS|P1|P2|00|LC1|LC2|...BODY...|LE1|LE2|  n = 10..65544
    //
    // LE, LE1, LE2 may be 0x00, 0x00|0x00 (means the maximum, 256 or 65536)
    // LC must not be 0x00 and LC1|LC2 must not be 0x00|0x00

    QByteArray out;
    const int n = aApdu.data.size();

    if (n <= 0xffff && aApdu.le <= 0x10000) {
        out.reserve(4);
        out.append(aApdu.cla);
        out.append(aApdu.ins);
        out.append(aApdu.p1);
        out.append(aApdu.p2);
        if (n > 0) {
            if (n <= 0xff) {
                /* Cases 3s and 4s */
                out.append((char) n);
            } else {
                /* Cases 3e and 4e */
                out.append('\0');
                out.append((char) (n >> 8));
                out.append((char) n);
            }
            out.append(aApdu.data);
        }
        if (aApdu.le > 0) {
            if (aApdu.le <= 0x100 && n <= 0xff) {
                /* Cases 2s and 4s */
                out.append((aApdu.le == 0x100) ? '\0' : ((char) aApdu.le));
            } else {
                /* Cases 4e and 2e */
                char le[2];

                if (aApdu.le == 0x10000) {
                    le[0] = le[1] = 0;
                } else {
                    le[0] = (char) (aApdu.le >> 8);
                    le[1] = (char) aApdu.le;
                }
                if (!n) {
                    /* Case 2e */
                    out.append('\0');
                }
                out.append(le, sizeof(le));
            }
        }
    }
    return out;
}

inline
YubiKeyUsbIo*
YubiKeyUsbIo::Tx::usbIo() const
{
    return qobject_cast<YubiKeyUsbIo*>(parent());
}

void
YubiKeyUsbIo::Tx::deactivate()
{
    if (iActive) {
        YubiKeyUsbIo::Private* priv = usbIo()->iPrivate;

        iActive = false;
        priv->deactivate();
        priv->emitQueuedSignals();
        if (iAutoDelete) {
            HarbourUtil::scheduleDeleteLater(this);
        }
    }
}

/* static */
void
YubiKeyUsbIo::Tx::dataSent(
    libusb_transfer* aTransfer)
{
    QPointer<Tx>* ptr = (QPointer<Tx>*) aTransfer->user_data;
    Tx* self = ptr->data();

    // Tx may be deallocated by now
    if (self) {
        if (aTransfer->status == LIBUSB_TRANSFER_COMPLETED) {
            HDEBUG("USB req" << self->iSeq << "sent");
        } else {
            HWARN("USB req" << self->iSeq << "failed," <<
                libusb_error_name(aTransfer->status));
            self->failed();
        }
    }

    delete ptr;
    free(aTransfer->buffer);
    libusb_free_transfer(aTransfer);
}

/* static */
void
YubiKeyUsbIo::Tx::dataReceived(
    libusb_transfer* aTransfer)
{
    QPointer<Tx>* ptr = (QPointer<Tx>*) aTransfer->user_data;
    Tx* self = ptr->data();

    // Tx may be deallocated by now
    if (self) {
        bool ok = false;
        const uint len = aTransfer->actual_length;
        const RDR_to_PC_MsgHeader* msg = (RDR_to_PC_MsgHeader*)
            aTransfer->buffer;

        // Expecting RDR_to_PC_DataBlock with at least 2 bytes
        if (aTransfer->status == LIBUSB_TRANSFER_COMPLETED &&
            len >= sizeof(*msg)) {
            const uint datalen = FROM_USB_ENDIAN(msg->dwLength);

            if (msg->bSeq != self->iSeq) {
                // Resubmit the transfer
                HWARN("Ignoring USB msg" << QByteArray((char*)msg, len).
                    toHex().constData());
                Private::submitTransfer(aTransfer);
                return;
            } else if (msg->bMessageType == RDR_to_PC_Message_DataBlock &&
                len >= (datalen + sizeof(RDR_to_PC_DataBlock))) {
                if (!datalen && (msg->bStatus & CCID_COMMAND_STATUS_MASK) ==
                    RDR_to_PC_CommandTimeExtension) {
                    // Time extension, resubmit the transfer
                    HDEBUG("Time extension" <<
                        QByteArray((char*)msg, len).toHex().constData());
                    Private::submitTransfer(aTransfer);
                    return;
                } else if (datalen >= 2) {
                    const RDR_to_PC_DataBlock* db = (RDR_to_PC_DataBlock*) msg;
                    const uchar* buf = (uchar*)(db + 1);
                    // Split R-APDU into data and status
                    const QByteArray data((char*) buf, datalen - 2);
                    const Result code(((uint)(buf[datalen - 2]) << 8) |
                        buf[datalen - 1]);

                    HDEBUG("USB xfr" << self->iSeq << "ok" <<
                        QByteArray((char*)msg, len).toHex().constData());
                    self->finished(code, data);
                    ok = true;
                }
            }
        }

        if (!ok) {
            if (aTransfer->status != LIBUSB_TRANSFER_COMPLETED) {
                HWARN("USB transfer status" << aTransfer->status);
            } else {
                HWARN("Unexpected USB msg" << QByteArray((char*) msg, len).
                    toHex().constData());
            }
            self->failed();
        }

        self->deactivate();
    }

    delete ptr;
    free(aTransfer->buffer);
    libusb_free_transfer(aTransfer);
}

void
YubiKeyUsbIo::Tx::failed()
{
    if (iState == TxPending) {
        iState = TxFailed;
        Q_EMIT txFailed();
    }
}

void
YubiKeyUsbIo::Tx::finished(
    Result aCode,
    QByteArray aData)
{
    if (iState == TxPending) {
        iState = TxFinished;
        Q_EMIT txFinished(aCode, aData);
    }
}

// ==========================================================================
// YubiKeyUsbIo
// ==========================================================================

YubiKeyUsbIo::YubiKeyUsbIo(
    Context aContext,
    libusb_device* aDevice,
    IntfAlt aInterface,
    QObject* aParent) :
    YubiKeyIo(aParent),
    iPrivate(new Private(this, aContext, aDevice, aInterface))
{}

YubiKeyUsbIo::~YubiKeyUsbIo()
{
    delete iPrivate;
}

const char*
YubiKeyUsbIo::ioPath() const
{
    return iPrivate->iPath.constData();
}

YubiKeyIo::Transport
YubiKeyUsbIo::ioTransport() const
{
    return USB;
}

YubiKeyIo::IoState
YubiKeyUsbIo::ioState() const
{
    return iPrivate->iState;
}

uint
YubiKeyUsbIo::ioSerial() const
{
    // YubiKey serial is not in the USB descriptor
    return 0;
}

YubiKeyIo::IoLock
YubiKeyUsbIo::ioLock()
{
    IoLock lock(iPrivate->lock());

    iPrivate->emitQueuedSignals();
    return lock;
}

YubiKeyIoTx*
YubiKeyUsbIo::ioTransmit(
    const APDU& aApdu)
{
    YubiKeyIoTx* tx = new Tx(this, aApdu);

    if (tx->txState() == YubiKeyIoTx::TxFailed) {
        // We have failed to actually submit USB transaction, drop this tx
        delete tx;
        return Q_NULLPTR;
    } else {
        iPrivate->setState(IoActive);
        iPrivate->emitQueuedSignals();
        return tx;
    }
}

void
YubiKeyUsbIo::gone()
{
    iPrivate->setState(IoTargetGone);
    iPrivate->emitQueuedSignals();
}

#include "YubiKeyUsbIo.moc"
