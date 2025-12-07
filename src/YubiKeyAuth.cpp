/*
 * Copyright (C) 2022-2025 Slava Monich <slava@monich.com>
 * Copyright (C) 2022 Jolla Ltd.
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

#include "foil_digest.h"
#include "foil_hmac.h"
#include "foil_kdf.h"

#include "YubiKeyAuth.h"
#include "YubiKeyConstants.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"
#include "HarbourUtil.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMap>
#include <QtCore/QMapIterator>
#include <QtCore/QSettings>

// ==========================================================================
// YubiKeyAuth::Private
// ==========================================================================

class YubiKeyAuth::Private :
    public QObject,
    public YubiKeyConstants
{
    Q_OBJECT

public:
    typedef QMap<YubiKeyAlgorithm, QByteArray> AuthKeyMap;
    typedef QMapIterator<YubiKeyAlgorithm, QByteArray> AuthKeyMapIterator;

    Private(const QByteArray&);
    ~Private();

    void connect(YubiKeyAuth*);
    bool setAccessKey(YubiKeyAlgorithm, const QByteArray&, bool);
    void clear();

    static GType digestType(YubiKeyAlgorithm);

Q_SIGNALS:
    void accessKeyChanged(YubiKeyAlgorithm);

public:
    static const QString AUTH_FILE;
    static const QDir gConfigDir;
    static QMap<QByteArray, Private*> gAuthMap;

public:
    QAtomicInt iRef;
    const QByteArray iYubiKeyId;
    QDir iConfigDir;
    const QString iAuthFile;
    QSettings* iSettings;
    AuthKeyMap iAccessKeys;
};

const QString YubiKeyAuth::Private::AUTH_FILE("auth");
const QDir YubiKeyAuth::Private::gConfigDir(YubiKeyUtil::configDir());
QMap<QByteArray, YubiKeyAuth::Private*> YubiKeyAuth::Private::gAuthMap;

YubiKeyAuth::Private::Private(
    const QByteArray& aYubiKeyId) :
    iRef(1),
    iYubiKeyId(aYubiKeyId),
    iConfigDir(gConfigDir.absoluteFilePath(HarbourUtil::toHex(aYubiKeyId))),
    iAuthFile(iConfigDir.filePath(AUTH_FILE)),
    iSettings(Q_NULLPTR)
{
    const QFileInfo authFile(iAuthFile);

    // Load settings from the file
    if (authFile.isFile() && authFile.isReadable()) {
        HDEBUG("Loading" << qPrintable(iAuthFile));
        iSettings = new QSettings(iAuthFile, QSettings::IniFormat, this);
        for (int i = 0; i < YubiKeyUtil::AllAlgorithms.count(); i++) {
            const YubiKeyAlgorithm alg = YubiKeyUtil::AllAlgorithms.at(i);
            const QByteArray key(YubiKeyUtil::fromHex(iSettings->
                value(YubiKeyUtil::algorithmName(alg)).toString()));

            if (!key.isEmpty()) {
                iAccessKeys.insert(alg, key);
            }
        }
    } else {
        HDEBUG(qPrintable(iAuthFile) << "doesn't exist");
    }

    gAuthMap.insert(iYubiKeyId, this);
}

YubiKeyAuth::Private::~Private()
{
    gAuthMap.remove(iYubiKeyId);
}

void
YubiKeyAuth::Private::connect(
    YubiKeyAuth* aAuth)
{
    aAuth->connect(this, SIGNAL(accessKeyChanged(YubiKeyAlgorithm)),
        SIGNAL(accessKeyChanged(YubiKeyAlgorithm)));
}

bool
YubiKeyAuth::Private::setAccessKey(
    YubiKeyAlgorithm aAlgorithm,
    const QByteArray& aAccessKey,
    bool aSave)
{
    if (aAlgorithm >= YubiKeyAlgorithm_Min &&
        aAlgorithm <= YubiKeyAlgorithm_Max &&
        iAccessKeys.value(aAlgorithm) != aAccessKey) {
        const QString algName(YubiKeyUtil::algorithmName(aAlgorithm));

        iAccessKeys.insert(aAlgorithm, aAccessKey);
        HDEBUG(qPrintable(HarbourUtil::toHex(iYubiKeyId)) << algName <<
            "=>" << qPrintable(HarbourUtil::toHex(aAccessKey)));

        if (aSave) {
            if (iConfigDir.exists() || iConfigDir.mkpath(".")) {
                HWARN("Writing" << qPrintable(iAuthFile));
                if (!iSettings) {
                    iSettings = new QSettings(iAuthFile, QSettings::IniFormat,
                        this);
                }
            } else {
                HWARN("Failed to create" << qPrintable(iConfigDir.path()));
            }
            iSettings->setValue(algName, HarbourUtil::toHex(aAccessKey));
        } else {
            // Remove the settings file without clearing the runtime keys
            delete iSettings;
            iSettings = Q_NULLPTR;

            if (QFile::remove(iAuthFile)) {
                HDEBUG("Removed" << qPrintable(iAuthFile));
            } else {
                HDEBUG("Failed to remove" << qPrintable(iAuthFile));
            }
        }

        Q_EMIT accessKeyChanged(aAlgorithm);
        return true;
    }
    return false;
}

void
YubiKeyAuth::Private::clear()
{
    const AuthKeyMap oldMap(iAccessKeys);
    AuthKeyMapIterator it(oldMap);

    iAccessKeys.clear();
    if (iSettings) {
        delete iSettings;
        iSettings = Q_NULLPTR;

        if (QFile::remove(iAuthFile)) {
            HDEBUG("Removed" << qPrintable(iAuthFile));
        } else {
            HDEBUG("Failed to remove" << qPrintable(iAuthFile));
        }
    }

    while (it.hasNext()) {
        Q_EMIT accessKeyChanged(it.next().key());
    }
}

/* static */
GType
YubiKeyAuth::Private::digestType(
    YubiKeyAlgorithm aAlgorithm)
{
    switch (aAlgorithm) {
    case YubiKeyAlgorithm_HMAC_SHA1: return FOIL_DIGEST_SHA1;
    case YubiKeyAlgorithm_HMAC_SHA256: return FOIL_DIGEST_SHA256;
    case YubiKeyAlgorithm_HMAC_SHA512: return FOIL_DIGEST_SHA512;
    case YubiKeyAlgorithm_Unknown: break;
    }
    return (GType)0;
}

// ==========================================================================
// YubiKeyAuth
// ==========================================================================

YubiKeyAuth::YubiKeyAuth(
    const QByteArray aYubiKeyId) :
    iPrivate(Private::gAuthMap.value(aYubiKeyId))
{
    qRegisterMetaType<YubiKeyAlgorithm>();
    if (iPrivate) {
        iPrivate->iRef.ref();
        iPrivate->connect(this);
    } else if (!aYubiKeyId.isEmpty()) {
        iPrivate = new Private(aYubiKeyId);
        iPrivate->connect(this);
    }
}

YubiKeyAuth::YubiKeyAuth(
    const YubiKeyAuth& aAuth) :
    QObject(Q_NULLPTR),
    iPrivate(aAuth.iPrivate)
{
    if (iPrivate) {
        iPrivate->iRef.ref();
        iPrivate->connect(this);
    }
}

YubiKeyAuth::YubiKeyAuth() :
    iPrivate(Q_NULLPTR)
{}

YubiKeyAuth::~YubiKeyAuth()
{
    if (iPrivate) {
        iPrivate->disconnect(this);
        if (!iPrivate->iRef.deref()) {
            delete iPrivate;
        }
    }
}

YubiKeyAuth&
YubiKeyAuth::operator=(
    const YubiKeyAuth& aAuth)
{
    if (iPrivate != aAuth.iPrivate) {
        if (iPrivate) {
            iPrivate->disconnect(this);
            if (!iPrivate->iRef.deref()) {
                delete iPrivate;
            }
        }
        iPrivate = aAuth.iPrivate;
        if (iPrivate) {
            iPrivate->iRef.ref();
            iPrivate->connect(this);
        }
    }
    return *this;
}

bool
YubiKeyAuth::isValid() const
{
    return iPrivate != Q_NULLPTR;
}

QByteArray
YubiKeyAuth::yubiKeyId() const
{
    return iPrivate ? iPrivate->iYubiKeyId : QByteArray();
}

QByteArray
YubiKeyAuth::getAccessKey(
    YubiKeyAlgorithm aAlgorithm) const
{
    return iPrivate ? iPrivate->iAccessKeys.value(aAlgorithm) : QByteArray();
}

bool
YubiKeyAuth::setAccessKey(
    YubiKeyAlgorithm aAlgorithm,
    const QByteArray aAccessKey,
    bool aSave)
{
    return iPrivate && iPrivate->setAccessKey(aAlgorithm, aAccessKey, aSave);
}

bool
YubiKeyAuth::setPassword(
    YubiKeyAlgorithm aAlgorithm,
    const QString aPassword,
    bool aSave)
{
    return iPrivate && iPrivate->setAccessKey(aAlgorithm,
        calculateAccessKey(iPrivate->iYubiKeyId, aAlgorithm, aPassword),
        aSave);
}

void
YubiKeyAuth::clearPassword()
{
    if (iPrivate) {
        iPrivate->clear();
    }
}

QByteArray
YubiKeyAuth::calculateAccessKey(
    const QByteArray aYubiKeyId,
    YubiKeyAlgorithm aAlgorithm,
    const QString aPassword)
{
    const QByteArray passwordUtf8(aPassword.toUtf8());
    FoilBytes salt;

    // The key is expected to be a user-supplied UTF-8 encoded
    // password passed through 1000 rounds of PBKDF2 with the ID
    // from select used as salt. 16 bytes of that are used.
    return YubiKeyUtil::toByteArray(foil_kdf_pbkdf2(
        Private::digestType(aAlgorithm),
        passwordUtf8.constData(), passwordUtf8.size(),
        YubiKeyUtil::fromByteArray(&salt, aYubiKeyId),
        YubiKeyConstants::KEY_ITER_COUNT, YubiKeyConstants::ACCESS_KEY_LEN));
}

QByteArray
YubiKeyAuth::calculateResponse(
    const QByteArray aAccessKey,
    const QByteArray aChallenge,
    YubiKeyAlgorithm aAlgorithm)
{
    if (!aAccessKey.isEmpty()) {
        FoilHmac* hmac = foil_hmac_new(Private::digestType(aAlgorithm),
            aAccessKey.constData(), aAccessKey.size());

        //
        // https://developers.yubico.com/OATH/YKOATH_Protocol.html
        //
        // VALIDATE INSTRUCTION
        //
        // ...The response if computed by performing the correct HMAC
        // function of the challenge with the correct key.
        foil_hmac_update(hmac, aChallenge.constData(), aChallenge.size());
        return YubiKeyUtil::toByteArray(foil_hmac_free_to_bytes(hmac));
    }
    return QByteArray();
}

#include "YubiKeyAuth.moc"
