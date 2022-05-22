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

#include "foil_digest.h"
#include "foil_hmac.h"
#include "foil_kdf.h"

#include "YubiKeyAuth.h"
#include "YubiKeyConstants.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#define AlgorithmIndex(a) ((a) - YubiKeyAlgorithm_Min)
#define AlgorithmCount (AlgorithmIndex(YubiKeyAlgorithm_Max) + 1)

// ==========================================================================
// YubiKeyAuth::Private
// ==========================================================================

class YubiKeyAuth::Private :
    public YubiKeyConstants
{
public:
    Private();
    ~Private();

    QSettings* getAuthSettings(const QByteArray);
    const QString authFilePath(const QByteArray);
    const QByteArray getAccessKey(const QByteArray, YubiKeyAlgorithm);
    bool setAccessKey(const QByteArray, YubiKeyAlgorithm, const QByteArray, bool);
    void clearPassword(const QByteArray, bool*);
    void clearAuthSettings(const QByteArray);

    static GType digestType(YubiKeyAlgorithm aAlgorithm);

public:
    static YubiKeyAuth* gInstance;
    static const QString AUTH_FILE;

public:
    QAtomicInt iRef;
    QMap<QByteArray, QByteArray> iAccessKeyMap[AlgorithmCount];

private:
    QDir iConfigDir;
    QMap<QByteArray, QSettings*> iAuthMap;
};

YubiKeyAuth* YubiKeyAuth::Private::gInstance = Q_NULLPTR;
const QString YubiKeyAuth::Private::AUTH_FILE("auth");

YubiKeyAuth::Private::Private() :
    iRef(1),
    iConfigDir(YubiKeyUtil::configDir())
{
}

YubiKeyAuth::Private::~Private()
{
    QMapIterator<QByteArray, QSettings*> it(iAuthMap);

    while (it.hasNext()) {
        delete it.next().value();
    }
}

const QString
YubiKeyAuth::Private::authFilePath(
    const QByteArray aYubiKeyId)
{
    return iConfigDir.absoluteFilePath(YubiKeyUtil::toHex(aYubiKeyId)) +
        QDir::separator() + AUTH_FILE;
}

QSettings*
YubiKeyAuth::Private::getAuthSettings(
    const QByteArray aYubiKeyId)
{
    QSettings* auth = iAuthMap.value(aYubiKeyId);

    if (!auth) {
        const QString keyDirName(YubiKeyUtil::toHex(aYubiKeyId));
        const QString keyDirPath(iConfigDir.absoluteFilePath(keyDirName));

        if (iConfigDir.mkpath(keyDirName)) {
            auth = new QSettings(keyDirPath + QDir::separator() + AUTH_FILE,
                QSettings::IniFormat);
            iAuthMap.insert(aYubiKeyId, auth);
        } else {
            HWARN("Failed to create" << qPrintable(keyDirPath));
        }
    }
    return auth;
}

const QByteArray
YubiKeyAuth::Private::getAccessKey(
    const QByteArray aYubiKeyId,
    YubiKeyAlgorithm aAlgorithm)
{
    if (aAlgorithm >= YubiKeyAlgorithm_Min &&
        aAlgorithm <= YubiKeyAlgorithm_Max &&
        !aYubiKeyId.isEmpty()) {
        QMap<QByteArray, QByteArray>* cache =
            iAccessKeyMap + AlgorithmIndex(aAlgorithm);
        QByteArray value(cache->value(aYubiKeyId));

        if (value.isEmpty()) {
            // No cached value, pick one from the settings file
            QSettings* settings = iAuthMap.value(aYubiKeyId);

            if (!settings) {
                // Load the config file
                const QString path(authFilePath(aYubiKeyId));
                const QFileInfo authFile(path);

                if (authFile.isFile() && authFile.isReadable()) {
                    HDEBUG("Loading" << qPrintable(path));
                    settings = new QSettings(path, QSettings::IniFormat);
                    iAuthMap.insert(aYubiKeyId, settings);
                } else {
                    HDEBUG(qPrintable(path) << "doesn't exist");
                }
            }
            if (settings) {
                if (!(value = YubiKeyUtil::fromHex(settings->value(YubiKeyUtil::
                    algorithmName(aAlgorithm)).toString())).isEmpty()) {
                    cache->insert(aYubiKeyId, value);
                }
            }
        }
        return value;
    }
    return QByteArray();
}

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

bool
YubiKeyAuth::Private::setAccessKey(
    const QByteArray aYubiKeyId,
    YubiKeyAlgorithm aAlgorithm,
    const QByteArray aAccessKey,
    bool aSave)
{
    if (aAlgorithm >= YubiKeyAlgorithm_Min &&
        aAlgorithm <= YubiKeyAlgorithm_Max) {
        const int mapIndex = AlgorithmIndex(aAlgorithm);

        if (!aSave) {
            // Remove the settings file even if the key didn't change
            clearAuthSettings(aYubiKeyId);
        }

        if (aAccessKey != iAccessKeyMap[mapIndex].value(aYubiKeyId)) {
            iAccessKeyMap[mapIndex].insert(aYubiKeyId, aAccessKey);
            HDEBUG(qPrintable(YubiKeyUtil::toHex(aYubiKeyId)) << aAlgorithm <<
                "=>" << qPrintable(YubiKeyUtil::toHex(aAccessKey)));
            if (aSave) {
                QSettings* settings = iAuthMap.value(aYubiKeyId);

                if (!settings) {
                    const QString path(authFilePath(aYubiKeyId));
                    const QFileInfo authFile(path);
                    QDir authFileDir(authFile.dir());

                    if (authFileDir.mkpath(".")) {
                        HWARN("Writing" << qPrintable(path));
                        settings = new QSettings(path, QSettings::IniFormat);
                        iAuthMap.insert(aYubiKeyId, settings);
                    } else {
                        HWARN("Failed to create" <<
                            qPrintable(authFileDir.absolutePath()));
                    }
                }
                if (settings) {
                    settings->setValue(YubiKeyUtil::algorithmName(aAlgorithm),
                        YubiKeyUtil::toHex(aAccessKey));
                }
            }
            return true;
        }
    }
    // Nothing has changed
    return false;
}

void
YubiKeyAuth::Private::clearAuthSettings(
    const QByteArray aYubiKeyId)
{
    QSettings* settings = iAuthMap.value(aYubiKeyId);

    if (settings) {
        const QString fileName(settings->fileName());

        iAuthMap.remove(aYubiKeyId);
        delete settings;
        settings = Q_NULLPTR;

        if (QFile::remove(fileName)) {
            HDEBUG("Removed" << qPrintable(fileName));
        } else {
            HDEBUG("Failed to remove" << qPrintable(fileName));
        }
    }
}

void
YubiKeyAuth::Private::clearPassword(
    const QByteArray aYubiKeyId,
    bool* aChanged)
{
    clearAuthSettings(aYubiKeyId);
    for (int i = 0; i < AlgorithmCount; i++) {
        aChanged[i] = (iAccessKeyMap[i].remove(aYubiKeyId) > 0);
    }
}

// ==========================================================================
// YubiKeyAuth
// ==========================================================================

YubiKeyAuth::YubiKeyAuth() :
    iPrivate(new Private)
{
    qRegisterMetaType<YubiKeyAlgorithm>();
    HASSERT(!Private::gInstance);
    Private::gInstance = this;
}

YubiKeyAuth::~YubiKeyAuth()
{
    delete iPrivate;

    HASSERT(Private::gInstance == this);
    Private::gInstance = Q_NULLPTR;
}

YubiKeyAuth*
YubiKeyAuth::get()
{
    if (Private::gInstance) {
        return Private::gInstance->ref();
    } else {
        return new YubiKeyAuth;
    }
}

YubiKeyAuth*
YubiKeyAuth::ref()
{
    iPrivate->iRef.ref();
    return this;
}

void
YubiKeyAuth::unref()
{
    if (!iPrivate->iRef.deref()) {
        delete this;
    }
}

const QByteArray
YubiKeyAuth::getAccessKey(
    const QByteArray aYubiKeyId,
    YubiKeyAlgorithm aAlgorithm) const
{
    return iPrivate->getAccessKey(aYubiKeyId, aAlgorithm);
}

bool
YubiKeyAuth::setAccessKey(
    const QByteArray aYubiKeyId,
    YubiKeyAlgorithm aAlgorithm,
    const QByteArray aAccessKey,
    bool aSave)
{
    if (iPrivate->setAccessKey(aYubiKeyId, aAlgorithm, aAccessKey, aSave)) {
        Q_EMIT accessKeyChanged(aYubiKeyId, aAlgorithm);
        return true;
    } else {
        return false;
    }
}

bool
YubiKeyAuth::setPassword(
    const QByteArray aYubiKeyId,
    YubiKeyAlgorithm aAlgorithm,
    const QString aPassword,
    bool aSave)
{
    return setAccessKey(aYubiKeyId, aAlgorithm,
        calculateAccessKey(aYubiKeyId, aAlgorithm, aPassword), aSave);
}

void
YubiKeyAuth::clearPassword(
    const QByteArray aYubiKeyId)
{
    bool changed[AlgorithmCount];

    // First update the state
    iPrivate->clearPassword(aYubiKeyId, changed);

    // Then emit signals
    for (int i = 0; i < AlgorithmCount; i++) {
        if (changed[i]) {
            Q_EMIT accessKeyChanged(aYubiKeyId, (YubiKeyAlgorithm)
                (YubiKeyAlgorithm_Min + i));
        }
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
