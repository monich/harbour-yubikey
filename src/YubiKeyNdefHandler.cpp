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

#include "YubiKeyNdefHandler.h"

#include "YubiKeyIo.h"

#include "HarbourDebug.h"

#include <QtCore/QPointer>
#include <QtCore/QUrl>
#include <QtCore/QTimer>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusError>

// ==========================================================================
// YubiKeyNdefHandler::Private
// ==========================================================================

class YubiKeyNdefHandler::Private:
    public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "harbour.yubikey.NDEF")
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
    static const QString YUBICO_DOMAIN;

public:
    enum Decision { Yes, No, MayBe };

    Private(YubiKeyNdefHandler*);
    ~Private() Q_DECL_OVERRIDE;

    Decision isYubiKeyConnected() const;
    bool setYubiKeyIo(YubiKeyIo*);
    bool havePendingRequest() const;
    void sendAsyncReply(int);

public Q_SLOTS:
    Q_SCRIPTABLE int HandleURI(QString, QDBusMessage);

private Q_SLOTS:
    void checkPendingRequest();
    void onYubiKeyIoDestroyed(QObject*);
    void onCallTimeout();

public:
    QTimer* iCallTimer;
    QPointer<YubiKeyIo> iYubiKeyIo;
    QDBusConnection iSystemBus;
    QDBusMessage iMessage;
    QString iUriHost;
    bool iRegisteredObject;
    bool iRegisteredService;
};

const QString YubiKeyNdefHandler::Private::PATH("/ndefhandler");
const QString YubiKeyNdefHandler::Private::INTERFACE("harbour.yubikey.NDEF");
const QString YubiKeyNdefHandler::Private::SERVICE("harbour.yubikey");
const QString YubiKeyNdefHandler::Private::YUBICO_DOMAIN("yubico.com");

YubiKeyNdefHandler::Private::Private(
    YubiKeyNdefHandler* aParent) :
    QDBusAbstractAdaptor(aParent),
    iCallTimer(new QTimer(this)),
    iSystemBus(QDBusConnection::systemBus()),
    iRegisteredObject(iSystemBus.registerObject(PATH, this,
        QDBusConnection::ExportScriptableSlots))
{
    iCallTimer->setSingleShot(true);
    iCallTimer->setInterval(1000);
    connect(iCallTimer, SIGNAL(timeout()), SLOT(onCallTimeout()));
    if (iRegisteredObject) {
        iRegisteredService = iSystemBus.registerService(SERVICE);
        if (iRegisteredService) {
            HDEBUG("Registered" << PATH << "object");
        } else {
            HWARN("Failed to register NDEF handler service" <<
                qPrintable(SERVICE) << iSystemBus.lastError());
        }
    } else {
        iRegisteredService = false;
        HWARN("Failed to register NDEF handler D-Bus object" <<
            iSystemBus.lastError());
    }
}

YubiKeyNdefHandler::Private::~Private()
{
    sendAsyncReply(false);
    if (iRegisteredObject) {
        iSystemBus.unregisterObject(PATH);
    }
    if (iRegisteredService) {
        iSystemBus.unregisterService(SERVICE);
    }
}

inline
bool
YubiKeyNdefHandler::Private::havePendingRequest() const
{
    return iMessage.type() == QDBusMessage::MethodCallMessage;
}

YubiKeyNdefHandler::Private::Decision
YubiKeyNdefHandler::Private::isYubiKeyConnected() const
{
    if (iYubiKeyIo) {
        switch (iYubiKeyIo->ioState()) {
        case YubiKeyIo::IoError:
        case YubiKeyIo::IoTargetGone:
        case YubiKeyIo::IoTargetInvalid:
            HDEBUG(iYubiKeyIo->ioPath() << "is not a YubiKey, ignoring");
            return No;
        case YubiKeyIo::IoLocking:
        case YubiKeyIo::IoLocked:
        case YubiKeyIo::IoReady:
        case YubiKeyIo::IoActive:
            HDEBUG(iYubiKeyIo->ioPath() << "seems to be YubiKey");
            return Yes;
        case YubiKeyIo::IoUnknown:
            break;
        }
    }
    // No idea what's going on
    return MayBe;
}

void
YubiKeyNdefHandler::Private::checkPendingRequest()
{
    if (havePendingRequest()) {
        switch (isYubiKeyConnected()) {
        case Yes:
            sendAsyncReply(true);
            break;
        case No:
            sendAsyncReply(false);
            break;
        case MayBe:
            break;
        }
    }
}

int
YubiKeyNdefHandler::Private::HandleURI(
    QString aUri,
    QDBusMessage aMessage)
{
    QUrl uri(aUri);
    HDEBUG(aMessage << aUri << "host" << uri.host());
    sendAsyncReply(false);

    // Try to handle it synchronously
    switch (isYubiKeyConnected()) {
    case Yes:
        return true;
    case No:
        return false;
    case MayBe:
        break;
    }

    // Keep the call pending until we figure it out (or the timeout expires)
    HDEBUG("Hmmm...");
    iMessage = aMessage;
    iMessage.setDelayedReply(true);
    iUriHost = uri.host().toLower();
    iCallTimer->start();
    if (iYubiKeyIo) {
        connect(iYubiKeyIo, SIGNAL(ioStateChanged(YubiKeyIo::IoState)),
            SLOT(checkPendingRequest()));
    }
    return false;
}

void
YubiKeyNdefHandler::Private::sendAsyncReply(
    int aResult)
{
    if (havePendingRequest()) {
        HDEBUG(aResult);
        iUriHost.clear();
        iSystemBus.send(iMessage.createReply(QVariant::fromValue<int>(aResult)));
        iMessage = QDBusMessage();
        // No longer need the timer and the I/O events
        iCallTimer->stop();
        if (iYubiKeyIo) {
            iYubiKeyIo->disconnect(this);
        }
    }
}

bool
YubiKeyNdefHandler::Private::setYubiKeyIo(
    YubiKeyIo* aYubiKeyIo)
{
    if (iYubiKeyIo != aYubiKeyIo) {
        if (iYubiKeyIo) {
            iYubiKeyIo->disconnect(this);
        }
        iYubiKeyIo = aYubiKeyIo;
        if (aYubiKeyIo) {
            HDEBUG(aYubiKeyIo->ioPath());
            checkPendingRequest();
            if (havePendingRequest()) {
                connect(iYubiKeyIo, SIGNAL(ioStateChanged(YubiKeyIo::IoState)),
                    SLOT(checkPendingRequest()));
            }
        } else {
            sendAsyncReply(false);
        }
        return true;
    }
    return false;
}

void
YubiKeyNdefHandler::Private::onYubiKeyIoDestroyed(
    QObject*)
{
    HDEBUG("I/O gone");
    sendAsyncReply(false);
}

void
YubiKeyNdefHandler::Private::onCallTimeout()
{
    HDEBUG("Timeout");
    // D-Bus call times out in case of a quick touch (the target disappears
    // before we have figured out what it is). Even though we don't really
    // know what this actually was, still swallow yubico.com URIs
    sendAsyncReply(iUriHost == YUBICO_DOMAIN ||
        iUriHost.endsWith("." + YUBICO_DOMAIN));
}

// ==========================================================================
// YubiKeyNdefHandler
// ==========================================================================

YubiKeyNdefHandler::YubiKeyNdefHandler(
    QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{}

YubiKeyIo*
YubiKeyNdefHandler::yubiKeyIo() const
{
    return iPrivate->iYubiKeyIo.data();
}

void
YubiKeyNdefHandler::setYubiKeyIo(
    YubiKeyIo* aYubiKeyIo)
{
    if (iPrivate->setYubiKeyIo(aYubiKeyIo)) {
        Q_EMIT yubiKeyIoChanged();
    }
}

#include "YubiKeyNdefHandler.moc"
