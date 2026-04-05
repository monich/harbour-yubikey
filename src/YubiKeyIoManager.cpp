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

#include "nfcdc_default_adapter.h"
#include "nfcdc_tag.h"

#include "YubiKeyIoManager.h"

#include "YubiKeyNfcIo.h"

#include "HarbourDebug.h"
#include "HarbourUtil.h"

#include <QtCore/QHash>
#include <QtCore/QSocketNotifier>

#include <poll.h>

class YubiKeyIoManager::Private
{
public:
    Private(YubiKeyIoManager*);
    // Since Impls are parented, they don't need to be explicitly destroyed

public:
    class Impl;
    #ifdef YUBIKEY_USB
    Impl* iUsb;
    #endif
    Impl* iNfc;
    Impl* iActiveImpl;
};

// ==========================================================================
// YubiKeyIoManager::Private::Impl
// ==========================================================================

class YubiKeyIoManager::Private::Impl:
    public QObject
{
public:
    class NFC;
    class USB;

    Impl(YubiKeyIoManager*);
    ~Impl();

    YubiKeyIoManager* parentObject() const;
    void setIo(YubiKeyIo*);

public:
    YubiKeyIo* iIo;
};

YubiKeyIoManager::Private::Impl::Impl(
    YubiKeyIoManager* aManager) :
    QObject(aManager),
    iIo(Q_NULLPTR)
{}

YubiKeyIoManager::Private::Impl::~Impl()
{
    delete iIo;
}

inline
YubiKeyIoManager*
YubiKeyIoManager::Private::Impl::parentObject() const
{
    return qobject_cast<YubiKeyIoManager*>(parent());
}

void YubiKeyIoManager::Private::Impl::setIo(
    YubiKeyIo* aIo)
{
    if (aIo != iIo) {
        YubiKeyIoManager* manager = parentObject();
        Private* priv = manager->iPrivate;

        if (iIo) {
            HDEBUG(iIo->ioPath() << "is gone");
            HarbourUtil::scheduleDeleteLater(iIo);
        }

        if ((iIo = aIo) != Q_NULLPTR) {
            HDEBUG(iIo->ioPath() << "arrived");
            // When a new device arrives, this implementation becomes
            // the active one.
            priv->iActiveImpl = this;
        }

        if (priv->iActiveImpl == this) {
            Q_EMIT manager->yubiKeyIoChanged();
        }
    }
}

// ==========================================================================
// YubiKeyIoManager::Private::Impl::NFC
// ==========================================================================

class YubiKeyIoManager::Private::Impl::NFC:
    public Impl
{
    Q_OBJECT

    enum AdapterEvents {
        ADAPTER_EVENT_VALID,
        ADAPTER_EVENT_TAGS,
        ADAPTER_EVENT_COUNT
    };

    enum TagEvents {
        TAG_EVENT_VALID,
        TAG_EVENT_PRESENT,
        TAG_EVENT_COUNT
    };

public:
    NFC(YubiKeyIoManager*);
    ~NFC();

    static void adapterEvent(NfcDefaultAdapter*, NFC_DEFAULT_ADAPTER_PROPERTY, void*);
    static void tagEvent(NfcTagClient*, NFC_TAG_PROPERTY, void*);

    YubiKeyIoManager* parentObject() const;
    void dropTag();
    void checkTags();

private:
    NfcDefaultAdapter* iAdapter;
    gulong iAdapterEventIds[ADAPTER_EVENT_COUNT];
    gulong iTagEventIds[TAG_EVENT_COUNT];
    NfcTagClient* iTag;
    YubiKeyIo* iIo;
};

YubiKeyIoManager::Private::Impl::NFC::NFC(
    YubiKeyIoManager* aParent) :
    Impl(aParent),
    iAdapter(nfc_default_adapter_new()),
    iTag(Q_NULLPTR)
{
    memset(iTagEventIds, 0, sizeof(iTagEventIds));
    iAdapterEventIds[ADAPTER_EVENT_VALID] =
        nfc_default_adapter_add_property_handler(iAdapter,
            NFC_DEFAULT_ADAPTER_PROPERTY_VALID, adapterEvent, this);
    iAdapterEventIds[ADAPTER_EVENT_TAGS] =
        nfc_default_adapter_add_property_handler(iAdapter,
            NFC_DEFAULT_ADAPTER_PROPERTY_TAGS, adapterEvent, this);
    checkTags();
}

YubiKeyIoManager::Private::Impl::NFC::~NFC()
{
    dropTag();
    nfc_default_adapter_remove_all_handlers(iAdapter, iAdapterEventIds);
    nfc_default_adapter_unref(iAdapter);
}

/* static  */
void
YubiKeyIoManager::Private::Impl::NFC::adapterEvent(
    NfcDefaultAdapter*,
    NFC_DEFAULT_ADAPTER_PROPERTY,
    void* aPrivate)
{
    ((NFC*)aPrivate)->checkTags();
}

/* static  */
void
YubiKeyIoManager::Private::Impl::NFC::tagEvent(
    NfcTagClient*,
    NFC_TAG_PROPERTY,
    void* aPrivate)
{
    ((NFC*)aPrivate)->checkTags();
}

void
YubiKeyIoManager::Private::Impl::NFC::dropTag()
{
    if (iTag) {
        nfc_tag_client_remove_all_handlers(iTag, iTagEventIds);
        nfc_tag_client_unref(iTag);
        iTag = Q_NULLPTR;
    }
}

void
YubiKeyIoManager::Private::Impl::NFC::checkTags()
{
    if (iAdapter->valid) {
        const char* activeTag = iAdapter->tags[0];

        if (activeTag) {
            if (iTag && strcmp(iTag->path, activeTag)) {
                dropTag();
            }
            if (!iTag) {
                iTag = nfc_tag_client_new(activeTag);
                iTagEventIds[TAG_EVENT_VALID] =
                    nfc_tag_client_add_property_handler(iTag,
                        NFC_TAG_PROPERTY_VALID, tagEvent, this);
                iTagEventIds[TAG_EVENT_PRESENT] =
                    nfc_tag_client_add_property_handler(iTag,
                        NFC_TAG_PROPERTY_PRESENT, tagEvent, this);
            }
        } else {
            dropTag();
        }

        // YubiKeyNfcIo gets created when the tag is valid and present
        setIo((iTag && iTag->valid && iTag->present) ?
            new YubiKeyNfcIo(iTag, this) :
            Q_NULLPTR);
    }
}

// ==========================================================================
// YubiKeyIoManager::Private::Impl::USB
// ==========================================================================

#ifdef YUBIKEY_USB

#include <libusb.h>

#include "YubiKeyUsbIo.h"

class YubiKeyIoManager::Private::Impl::USB:
    public Impl
{
    Q_OBJECT

public:
    enum { YUBIKEY_VID = 0x1050 };
    enum {
        HOTPLUG_EVENT_DEVICE_ARRIVED,
        HOTPLUG_EVENT_DEVICE_LEFT,
        HOTPLUG_EVENT_COUNT
    };

    USB(YubiKeyIoManager*);
    ~USB();

    static void libusbPollfdAdded(int, short, void*);
    static void libusbPollfdRemoved(int, void*);
    static int libusbDeviceArrived(libusb_context*, libusb_device*, libusb_hotplug_event, void*);
    static int libusbDeviceLeft(libusb_context*, libusb_device*, libusb_hotplug_event, void*);
    static bool isYubiKey(libusb_device*, YubiKeyUsbIo::IntfAlt*);
    static void dropNotifiers(QHash<int, QSocketNotifier*>&);

    void pollfdAdded(int, short);
    void pollfdRemoved(int);
    void deviceArrived(libusb_device*);
    void deviceLeft(libusb_device*);

private Q_SLOTS:
    void libusbPollEvent(int);

private:
    YubiKeyUsbIo::Context iContext;
    QHash<int, QSocketNotifier*> iReadNotifiers;
    QHash<int, QSocketNotifier*> iWriteNotifiers;
    libusb_hotplug_callback_handle iHotplugEvents[HOTPLUG_EVENT_COUNT];
    libusb_device* iDevice;
};

YubiKeyIoManager::Private::Impl::USB::USB(
    YubiKeyIoManager* aParent) :
    Impl(aParent),
    iDevice(Q_NULLPTR)
{
    memset(iHotplugEvents, 0, sizeof(iHotplugEvents));

    // Setup event polling
    const libusb_pollfd** pollfds = libusb_get_pollfds(iContext);

    if (pollfds) {
        const libusb_pollfd** ptr = pollfds;

        while (*ptr) {
            const libusb_pollfd* p = *ptr++;

            pollfdAdded(p->fd, p->events);
        }
        libusb_free_pollfds(pollfds);
    }
    libusb_set_pollfd_notifiers(iContext, libusbPollfdAdded,
        libusbPollfdRemoved, this);

    // Setup hotplug
    libusb_hotplug_register_callback(iContext,
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, (libusb_hotplug_flag) 0,
        YUBIKEY_VID, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
        libusbDeviceArrived, this,
        iHotplugEvents + HOTPLUG_EVENT_DEVICE_ARRIVED);
    libusb_hotplug_register_callback(iContext,
        LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, (libusb_hotplug_flag) 0,
        YUBIKEY_VID, LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
        libusbDeviceLeft, this,
        iHotplugEvents + HOTPLUG_EVENT_DEVICE_LEFT);

    // Check if the device is already plugged in
    libusb_device** devs = Q_NULLPTR;

    libusb_get_device_list(iContext, &devs);
    if (devs) {
        YubiKeyUsbIo::IntfAlt intf;
        libusb_device** ptr = devs;
        libusb_device* dev;

        memset(&intf, 0, sizeof(intf));
        while ((dev = *ptr++) != Q_NULLPTR) {
            if (isYubiKey(dev, &intf)) {
                iDevice = libusb_ref_device(dev);
                iIo = new YubiKeyUsbIo(iContext, dev, intf, this);
                break;
            }
        }
        libusb_free_device_list(devs, true);
    }
}

YubiKeyIoManager::Private::Impl::USB::~USB()
{
    dropNotifiers(iReadNotifiers);
    dropNotifiers(iWriteNotifiers);
    for (int i = 0; i < HOTPLUG_EVENT_COUNT; i++) {
        if (iHotplugEvents[i]) {
            libusb_hotplug_deregister_callback(iContext, iHotplugEvents[i]);
        }
    }
    libusb_set_pollfd_notifiers(iContext, Q_NULLPTR, Q_NULLPTR, Q_NULLPTR);
    if (iDevice) {
        libusb_unref_device(iDevice);
    }
}

/* static */
void
YubiKeyIoManager::Private::Impl::USB::libusbPollfdAdded(
    int aFd,
    short aEvents,
    void* aThis)
{
    ((USB*)aThis)->pollfdAdded(aFd, aEvents);
}

/* static */
void
YubiKeyIoManager::Private::Impl::USB::libusbPollfdRemoved(
    int aFd,
    void* aThis)
{
    ((USB*)aThis)->pollfdRemoved(aFd);
}

/* static */
int
YubiKeyIoManager::Private::Impl::USB::libusbDeviceArrived(
    libusb_context*,
    libusb_device* aDevice,
    libusb_hotplug_event,
    void* aThis)
{
    ((USB*)aThis)->deviceArrived(aDevice);
    return 0;
}

/* static */
int
YubiKeyIoManager::Private::Impl::USB::libusbDeviceLeft(
    libusb_context*,
    libusb_device* aDevice,
    libusb_hotplug_event,
    void* aThis)
{
    ((USB*)aThis)->deviceLeft(aDevice);
    return 0;
}

/* static */
void
YubiKeyIoManager::Private::Impl::USB::dropNotifiers(
    QHash<int, QSocketNotifier*>& aNotifiers)
{
    QMutableHashIterator<int, QSocketNotifier*> it(aNotifiers);
    while (it.hasNext()) {
        delete it.next().value();
        it.remove();
    }
}

/* static */
bool
YubiKeyIoManager::Private::Impl::USB::isYubiKey(
    libusb_device* aDev,
    YubiKeyUsbIo::IntfAlt* aIntf)
{
    libusb_device_descriptor desc;
    bool yubikey = false;

    memset(&desc, 0, sizeof(desc));
    libusb_get_device_descriptor(aDev, &desc);
    if (desc.idVendor == YUBIKEY_VID && desc.bNumConfigurations == 1) {
        libusb_config_descriptor* config = Q_NULLPTR;

        if (libusb_get_config_descriptor(aDev, 0, &config) == LIBUSB_SUCCESS) {
            for (uint i = 0; i < config->bNumInterfaces && !yubikey; i++) {
                const libusb_interface* intf = config->interface + i;

                for (int a = 0; a < intf->num_altsetting; a++) {
                    if (intf->altsetting[a].bInterfaceClass ==
                        LIBUSB_CLASS_SMART_CARD) {
                        HDEBUG("USB YubiKey" << hex << desc.idVendor <<
                            desc.idProduct);
                        HDEBUG("CCID Intf/Setting" << i << a);
                        aIntf->iIntfNum = i;
                        aIntf->iAltSetting = a;
                        yubikey = true;
                        break;
                    }
                }
            }
            libusb_free_config_descriptor(config);
        }
    }
    return yubikey;
}

void
YubiKeyIoManager::Private::Impl::USB::pollfdAdded(
    int aFd,
    short aEvents)
{
    HDEBUG(aFd << hex << aEvents);
    if (aEvents & POLLIN) {
        QSocketNotifier* sn= new QSocketNotifier(aFd, QSocketNotifier::Read, this);

        connect(sn, SIGNAL(activated(int)), this, SLOT(libusbPollEvent(int)));
        delete iReadNotifiers.take(aFd);
        iReadNotifiers.insert(aFd, sn);
    }
    if (aEvents & POLLOUT) {
        QSocketNotifier* sn= new QSocketNotifier(aFd, QSocketNotifier::Write, this);

        connect(sn, SIGNAL(activated(int)), this, SLOT(libusbPollEvent(int)));
        delete iWriteNotifiers.take(aFd);
        iWriteNotifiers.insert(aFd, sn);
    }
}

void
YubiKeyIoManager::Private::Impl::USB::pollfdRemoved(
    int aFd)
{
    HDEBUG(aFd);
    delete iReadNotifiers.take(aFd);
    delete iWriteNotifiers.take(aFd);
}

void
YubiKeyIoManager::Private::Impl::USB::libusbPollEvent(
    int aFd)
{
    struct timeval tv;

    HDEBUG(aFd);
    memset(&tv, 0, sizeof(tv));
    libusb_handle_events_timeout(iContext, &tv);
}

void
YubiKeyIoManager::Private::Impl::USB::deviceArrived(
    libusb_device* aDevice)
{
    YubiKeyUsbIo::IntfAlt intf;

    memset(&intf, 0, sizeof(intf));
    if (isYubiKey(aDevice, &intf)) {
        if (!iDevice) {
            HDEBUG("YubiKey arrived");
            iDevice = libusb_ref_device(aDevice);
            setIo(new YubiKeyUsbIo(iContext, aDevice, intf, this));
        }
    }
}

void
YubiKeyIoManager::Private::Impl::USB::deviceLeft(
    libusb_device* aDevice)
{
    if (iDevice && iDevice == aDevice) {
        HDEBUG("YubiKey is gone");
        static_cast<YubiKeyUsbIo*>(iIo)->gone();
        libusb_unref_device(iDevice);
        iDevice = Q_NULLPTR;
        setIo(Q_NULLPTR);
    }
}

#endif // YUBIKEY_USB

// ==========================================================================
// YubiKeyIoManager::Private
// ==========================================================================

YubiKeyIoManager::Private::Private(
    YubiKeyIoManager* aManager) :
    #ifdef YUBIKEY_USB
    iUsb(new Impl::USB(aManager)),
    #endif
    iNfc(new Impl::NFC(aManager)),
    iActiveImpl(iNfc)
{
#ifdef YUBIKEY_USB
    // The USB key may be already plugged in
    if (iUsb->iIo) {
        iActiveImpl = iUsb;
    }
#endif
}

// Since Impls are parented, they don't need to be explicitly destroyed

// ==========================================================================
// YubiKeyIoManager
// ==========================================================================

YubiKeyIoManager::YubiKeyIoManager(
    QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{}

YubiKeyIoManager::~YubiKeyIoManager()
{
    delete iPrivate;
}

YubiKeyIo*
YubiKeyIoManager::yubiKeyIo() const
{
    return iPrivate->iActiveImpl->iIo;
}

#include "YubiKeyIoManager.moc"
