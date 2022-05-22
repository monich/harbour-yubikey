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

#include <nfcdc_default_adapter.h>
#include <nfcdc_isodep.h>

#include <gutil_misc.h>

#include "YubiKeyRecognizer.h"
#include "YubiKeyTag.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusError>

// ==========================================================================
// YubiKeyRecognizer::Handler
// ==========================================================================

#define ISODEP_VALID_CHANGED isoDepValidChanged

class YubiKeyRecognizer::Handler:
    public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "harbour.yubikey")
    Q_CLASSINFO("D-Bus Introspection",
"  <interface name=\"harbour.yubikey.NDEF\">\n"
"    <method name=\"HandleURI\">\n"
"      <arg direction=\"in\" type=\"s\" name=\"uri\"/>\n"
"      <arg direction=\"out\" type=\"i\" name=\"status\"/>\n"
"    </method>\n"
"  </interface>\n")

    static const QString PATH;
    static const QString INTERFACE;
    static const QString SERVICE;

public:
    Handler(QObject* aParent);
    ~Handler();

    void handleTagStateChange(YubiKeyTag*);
    void dropIsoDepTag();
    void sendAsyncReply(int aResult);

    Q_INVOKABLE void ISODEP_VALID_CHANGED();
    static void isoDepValidEvent(NfcIsoDepClient*, NFC_ISODEP_PROPERTY, void*);
    static bool isYubiKeyHB(const GUtilData*);


public Q_SLOTS:
    int HandleURI(QString, QDBusMessage);

private:
    QDBusConnection iSystemBus;
    QDBusMessage iMessage;
    bool iRegisteredObject;
    bool iRegisteredService;
    NfcDefaultAdapter* iAdapter;
    NfcIsoDepClient* iIsoDep;
    gulong iIsoDepValidId;
};

const QString YubiKeyRecognizer::Handler::PATH("/ndefhandler");
const QString YubiKeyRecognizer::Handler::INTERFACE("harbour.yubikey.NDEF");
const QString YubiKeyRecognizer::Handler::SERVICE("harbour.yubikey");

YubiKeyRecognizer::Handler::Handler(
    QObject* aParent) :
    QDBusAbstractAdaptor(aParent),
    iSystemBus(QDBusConnection::systemBus()),
    iAdapter(nfc_default_adapter_new()),
    iIsoDep(Q_NULLPTR),
    iIsoDepValidId(0)
{
    iRegisteredObject = iSystemBus.registerObject(PATH, INTERFACE, this,
        QDBusConnection::ExportAllSlots);
    if (!iRegisteredObject) {
        iRegisteredService = false;
        HWARN("Failed to register NDEF handler D-Bus object" <<
            iSystemBus.lastError());
    } else {
        iRegisteredService = iSystemBus.registerService(SERVICE);
        if (!iRegisteredService) {
            HWARN("Failed to register NDEF handler service" << qPrintable(SERVICE) <<
                iSystemBus.lastError());
        }
    }
}

YubiKeyRecognizer::Handler::~Handler()
{
    sendAsyncReply(FALSE);
    dropIsoDepTag();
    nfc_default_adapter_unref(iAdapter);
    if (iRegisteredObject) {
        iSystemBus.unregisterObject(PATH);
    }
    if (iRegisteredService) {
        iSystemBus.unregisterService(SERVICE);
    }
}

void
YubiKeyRecognizer::Handler::sendAsyncReply(
    int aResult)
{
    if (iMessage.type() == QDBusMessage::MethodCallMessage) {
        iSystemBus.send(iMessage.createReply(QVariant::fromValue<int>(aResult)));
        iMessage = QDBusMessage();
    }
}

void
YubiKeyRecognizer::Handler::dropIsoDepTag()
{
    if (iIsoDep) {
        nfc_isodep_client_remove_handler(iIsoDep, iIsoDepValidId);
        nfc_isodep_client_unref(iIsoDep);
        iIsoDep = Q_NULLPTR;
        iIsoDepValidId = 0;
    }
}

bool
YubiKeyRecognizer::Handler::isYubiKeyHB(
    const GUtilData* aHb)
{
    static const GUtilData yubiKeySuffix = { (const guint8*) "YubiKey", 7 };

    if (gutil_data_has_suffix(aHb, &yubiKeySuffix)) {
        HDEBUG("This is a YubiKey based on HB");
        return true;
    }
    return false;
}

int
YubiKeyRecognizer::Handler::HandleURI(
    QString aUri,
    QDBusMessage aMessage)
{
    HDEBUG(aMessage << aUri);
    sendAsyncReply(FALSE);
    dropIsoDepTag();
    if (iAdapter->tags[0]) {
        HDEBUG(iAdapter->tags[0]);
        iIsoDep = nfc_isodep_client_new(iAdapter->tags[0]);
        if (iIsoDep->valid) {
            const gboolean isodep = iIsoDep->present;

            // Don't need it anymore
            nfc_isodep_client_unref(iIsoDep);
            iIsoDep = Q_NULLPTR;
            if (isodep) {
                if (isYubiKeyHB(nfc_isodep_client_act_param(iIsoDep,
                    NFC_ISODEP_ACT_PARAM_HB))) {
                    return TRUE;
                }
                HDEBUG("At least this is an ISO-DEP");
            } else {
                HDEBUG("Not even an ISO-DEP!");
                return FALSE;
            }
        } else {
            // Wait until the initial query completes
            iIsoDepValidId = nfc_isodep_client_add_property_handler(iIsoDep,
                NFC_ISODEP_PROPERTY_VALID, isoDepValidEvent, this);
        }
        // Can't tell right now what kind of tag that is
        iMessage = aMessage;
        iMessage.setDelayedReply(true);
    }
    return FALSE;
}

void
YubiKeyRecognizer::Handler::isoDepValidEvent(
    NfcIsoDepClient*,
    NFC_ISODEP_PROPERTY,
    void* aHandler)
{
    // See https://bugreports.qt.io/browse/QTBUG-18434
    QMetaObject::invokeMethod((QObject*)aHandler,
        G_STRINGIFY(ISODEP_VALID_CHANGED));
}

void
YubiKeyRecognizer::Handler::ISODEP_VALID_CHANGED()
{
    if (iIsoDep->valid) {
        if (iIsoDep->present) {
            if (isYubiKeyHB(nfc_isodep_client_act_param(iIsoDep,
                NFC_ISODEP_ACT_PARAM_HB))) {
                sendAsyncReply(TRUE);
            } else {
                HDEBUG("At least this is an ISO-DEP");
            }
        } else {
            sendAsyncReply(FALSE);
        }
        dropIsoDepTag();
    }
}

void
YubiKeyRecognizer::Handler::handleTagStateChange(
    YubiKeyTag* aTag)
{
    if (iMessage.type() == QDBusMessage::MethodCallMessage) {
        const YubiKeyTag::TagState state = aTag->tagState();
        int result = -1;

        HDEBUG(state);
        switch (state) {
        case YubiKeyTag::TagChecking:
            break;
        case YubiKeyTag::TagNone:
        case YubiKeyTag::TagUnrecognized:
            result = FALSE;
            break;
        case YubiKeyTag::TagYubiKeyReady:
            result = TRUE;
            break;
        }
        if (result >= 0) {
            HDEBUG((result ? "Looks" : "Doesn't look") << "like a YubiKey");
            sendAsyncReply(result);
            dropIsoDepTag();
        }
    }
}

// ==========================================================================
// YubiKeyRecognizer::Private
// ==========================================================================

// s(SignalName,signalName)
#define YUBIKEY_RECOGNIZER_SIGNALS(s) \
    s(YubiKeyVersion,yubiKeyVersion) \
    s(YubiKeyId,yubiKeyId) // Must be signalled after YubiKeyVersion

class YubiKeyRecognizer::Private:
    public QObject
{
    Q_OBJECT

    enum Signal {
#define SIGNAL_ENUM_(Name,name) Signal##Name##Changed,
        YUBIKEY_RECOGNIZER_SIGNALS(SIGNAL_ENUM_)
#undef  SIGNAL_ENUM_
        SignalCount
    };

    typedef uint SignalMask;
    typedef void (YubiKeyRecognizer::*Method)();
    typedef void (*SignalEmitter)(YubiKeyRecognizer*, Signal);

    static void emitSignal(YubiKeyRecognizer*, Signal);
    static void invokeSignal(YubiKeyRecognizer*, Signal);

public:
    Private(YubiKeyRecognizer* aParent);
    ~Private();

    YubiKeyRecognizer* parentObject();
    void queueSignal(Signal aSignal);
    void emitQueuedSignals(SignalEmitter);
    void clearState();
    void dropTag();
    void updatePath(const char*);
    void updateYubiKeyId();
    void updateYubiKeyVersion();

    static void staticAdapterEvent(NfcDefaultAdapter*, NFC_DEFAULT_ADAPTER_PROPERTY, void*);

public Q_SLOTS:
    void onTagStateChanged();
    void onTagYubiKeyIdChanged();
    void onTagYubiKeyVersionChanged();

public:
    SignalMask iQueuedSignals;
    Signal iFirstQueuedSignal;
    NfcDefaultAdapter* iAdapter;
    gulong iAdapterEventId;
    QByteArray iLastId;
    QByteArray iLastVersion;
    QString iLastIdString;
    QString iLastVersionString;
    Handler* iHandler;
    YubiKeyTag* iTag;
};

YubiKeyRecognizer::Private::Private(
    YubiKeyRecognizer* aParent) :
    QObject(aParent),
    iQueuedSignals(0),
    iFirstQueuedSignal(SignalCount),
    iAdapter(nfc_default_adapter_new()),
    iHandler(new Handler(aParent)),
    iTag(Q_NULLPTR)
{
    iAdapterEventId = nfc_default_adapter_add_property_handler(iAdapter,
        NFC_DEFAULT_ADAPTER_PROPERTY_TAGS, staticAdapterEvent, this);
    updatePath(iAdapter->tags[0]);
    iFirstQueuedSignal = SignalCount; // Clear queued events
}

YubiKeyRecognizer::Private::~Private()
{
    nfc_default_adapter_remove_handler(iAdapter, iAdapterEventId);
    nfc_default_adapter_unref(iAdapter);
    dropTag();
    delete iHandler;
}

inline
YubiKeyRecognizer*
YubiKeyRecognizer::Private::parentObject()
{
    return qobject_cast<YubiKeyRecognizer*>(parent());
}

void
YubiKeyRecognizer::Private::emitSignal(
    YubiKeyRecognizer* aObject,
    Signal aSignal)
{
    static const Method method [] = {
#define SIGNAL_EMITTER_(Name,name) &YubiKeyRecognizer::name##Changed,
        YUBIKEY_RECOGNIZER_SIGNALS(SIGNAL_EMITTER_)
#undef  SIGNAL_EMITTER_
    };
    Q_STATIC_ASSERT(G_N_ELEMENTS(method) == SignalCount);
    HASSERT(aSignal < SignalCount);
    Q_EMIT (aObject->*(method[aSignal]))();
}

void
YubiKeyRecognizer::Private::invokeSignal(
    YubiKeyRecognizer* aObject,
    Signal aSignal)
{
    static const char* name [] = {
#define SIGNAL_NAME_(Name,name) G_STRINGIFY(name##Changed),
        YUBIKEY_RECOGNIZER_SIGNALS(SIGNAL_NAME_)
#undef  SIGNAL_NAME_
    };
    Q_STATIC_ASSERT(G_N_ELEMENTS(name) == SignalCount);
    HASSERT(aSignal < SignalCount);
    // See https://bugreports.qt.io/browse/QTBUG-18434
    QMetaObject::invokeMethod(aObject, name[aSignal]);
}

void
YubiKeyRecognizer::Private::queueSignal(
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
YubiKeyRecognizer::Private::emitQueuedSignals(
    SignalEmitter aEmitter)
{
    if (iQueuedSignals) {
        YubiKeyRecognizer* obj = parentObject();
        // Reset first queued signal before emitting the signals.
        // Signal handlers may emit more signals.
        uint i = iFirstQueuedSignal;
        iFirstQueuedSignal = SignalCount;
        for (; i < SignalCount && iQueuedSignals; i++) {
            const SignalMask signalBit = (SignalMask(1) << i);
            if (iQueuedSignals & signalBit) {
                iQueuedSignals &= ~signalBit;
                aEmitter(obj, Signal(i));
            }
        }
    }
}

void
YubiKeyRecognizer::Private::clearState()
{
    if (!iLastId.isEmpty()) {
        iLastId.clear();
        iLastIdString.clear();
        queueSignal(SignalYubiKeyIdChanged);
    }
    if (!iLastVersion.isEmpty()) {
        iLastVersion.clear();
        iLastVersionString.clear();
        queueSignal(SignalYubiKeyVersionChanged);
    }
    emitQueuedSignals(emitSignal);
}

void
YubiKeyRecognizer::Private::dropTag()
{
    if (iTag) {
        iTag->disconnect(this);
        iTag->put();
        iTag = Q_NULLPTR;
    }
}

void
YubiKeyRecognizer::Private::updatePath(
    const char* aPath)
{
    if (aPath) {
        const QString path(QString::fromLatin1(aPath));

        if (!iTag || iTag->path() != path) {
            dropTag();
            iTag = YubiKeyTag::get(path);
            connect(iTag,
                SIGNAL(tagStateChanged()),
                SLOT(onTagStateChanged()));
            connect(iTag,
                SIGNAL(yubiKeyIdChanged()),
                SLOT(onTagYubiKeyIdChanged()));
            connect(iTag,
                SIGNAL(yubiKeyVersionChanged()),
                SLOT(onTagYubiKeyVersionChanged()));
            updateYubiKeyId();
            updateYubiKeyVersion();
        }
    } else {
        dropTag();
    }
}

void
YubiKeyRecognizer::Private::updateYubiKeyId()
{
    const QByteArray id(iTag->yubiKeyId());

    if (!id.isEmpty() && id != iLastId) {
        iLastId = id;
        iLastIdString = iTag->yubiKeyIdString();
        HDEBUG(qPrintable(iLastIdString));
        queueSignal(SignalYubiKeyIdChanged);
    }
}

void
YubiKeyRecognizer::Private::updateYubiKeyVersion()
{
    const QByteArray version(iTag->yubiKeyVersion());

    if (!version.isEmpty() && version != iLastVersion) {
        iLastVersion = version;
        iLastVersionString = iTag->yubiKeyVersionString();
        HDEBUG(qPrintable(iLastVersionString));
        queueSignal(SignalYubiKeyVersionChanged);
    }
}

void
YubiKeyRecognizer::Private::onTagStateChanged()
{
    iHandler->handleTagStateChange(iTag);
}

void
YubiKeyRecognizer::Private::onTagYubiKeyIdChanged()
{
    updateYubiKeyId();
    emitQueuedSignals(emitSignal);
}

void
YubiKeyRecognizer::Private::onTagYubiKeyVersionChanged()
{
    updateYubiKeyVersion();
    emitQueuedSignals(emitSignal);
}

void
YubiKeyRecognizer::Private::staticAdapterEvent(
    NfcDefaultAdapter* aAdapter,
    NFC_DEFAULT_ADAPTER_PROPERTY,
    void* aSelf)
{
    Private* self = (Private*)aSelf;

    self->updatePath(aAdapter->tags[0]);
    self->emitQueuedSignals(invokeSignal);
}

// ==========================================================================
// YubiKeyRecognizer
// ==========================================================================

YubiKeyRecognizer::YubiKeyRecognizer(
    QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{
}

QObject*
YubiKeyRecognizer::createSingleton(
    QQmlEngine*,
    QJSEngine*)
{
    return new YubiKeyRecognizer;
}

QString
YubiKeyRecognizer::yubiKeyId() const
{
    return iPrivate->iLastIdString;
}

QString
YubiKeyRecognizer::yubiKeyVersion() const
{
    return iPrivate->iLastVersionString;
}

void
YubiKeyRecognizer::clearState()
{
    return iPrivate->clearState();
}

#include "YubiKeyRecognizer.moc"
