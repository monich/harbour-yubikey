/*
 * Copyright (C) 2022-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2022 Jolla Ltd.
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

#include "gutil_misc.h"

#include "YubiKeyDefs.h"
#include "YubiKeyUtil.h"
#include "YubiKeyConstants.h"

#include "HarbourBase32.h"
#include "HarbourDebug.h"

#include <QStandardPaths>

const QString YubiKeyUtil::ALGORITHM_SHA1("SHA1");
const QString YubiKeyUtil::ALGORITHM_SHA256("SHA256");
const QString YubiKeyUtil::ALGORITHM_SHA512("SHA512");

QDir
YubiKeyUtil::configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
        QStringLiteral("/" YUBIKEY_APP_NAME);
}

QDir
YubiKeyUtil::configDir(
    const QByteArray aYubiKeyId)
{
    return QDir(configDir().absoluteFilePath(YubiKeyUtil::toHex(aYubiKeyId)));
}

uint
YubiKeyUtil::versionToNumber(
    const uchar* aData,
    gsize aSize)
{
    // The version is typically 3 bytes long
    uint v = 0;
    for (gsize i = 0; i < aSize; i++) {
        v = (v << 8) | aData[i];
    }
    return v;
}

QString
YubiKeyUtil::versionToString(
    const uchar* aData,
    gsize aSize)
{
    QString str;
    if (aSize > 0) {
        str.reserve(2 * aSize - 1);
        for (gsize i = 0; i < aSize; i++) {
            if (!str.isEmpty()) {
                str.append(QChar('.'));
            }
            str.append(QString::number(aData[i]));
        }
    }
    return str;
}

const FoilBytes*
YubiKeyUtil::fromByteArray(
    FoilBytes* aBytes,
    const QByteArray& aData)
{
    aBytes->val = (guint8*)aData.constData();
    aBytes->len = aData.size();
    return aBytes;
}

QByteArray
YubiKeyUtil::toByteArray(
    const GUtilData* aData)
{
    return (aData && aData->size) ?
        QByteArray((const char*)aData->bytes, aData->size) :
        QByteArray();
}

QByteArray
YubiKeyUtil::toByteArray(
    GBytes* aBytes,
    bool aDropBytes)
{
    if (aBytes) {
        gsize size;
        const void* data = g_bytes_get_data(aBytes, &size);

        const QByteArray result((char*)data, (int)size);
        if (aDropBytes) {
            g_bytes_unref(aBytes);
        }
        return result;
    }
    return QByteArray();
}

QByteArray
YubiKeyUtil::fromHex(
    const QString aHex)
{
    if (!aHex.isEmpty() && !(aHex.length() & 1)) {
        const QByteArray hex(aHex.toLatin1());
        const int len = hex.size();
        if (len == aHex.length()) {
            QByteArray bytes;
            bytes.resize(len/2);
            if (gutil_hex2bin(hex.data(), len, bytes.data())) {
                return bytes;
            }
        }
    }
    return QByteArray();
}

QString
YubiKeyUtil::toHex(
    const uchar* aData,
    gsize aSize)
{
    QString str;
    if (aSize > 0) {
        str.reserve(2*aSize);
        for (gsize i = 0; i < aSize; i++) {
            static const char hex[] = "0123456789abcdef";
            const uchar b = aData[i];
            str.append(QChar(hex[(b & 0xf0) >> 4]));
            str.append(QChar(hex[b & 0x0f]));
        }
    }
    return str;
}

uchar
YubiKeyUtil::readTLV(
    GUtilRange* aRange,
    GUtilData* aData)
{
    if ((aRange->ptr + 1) < aRange->end && aRange->ptr[0] &&
        (aRange->ptr + 2 + aRange->ptr[1]) <= aRange->end) {
        const uchar t = aRange->ptr[0];

        aData->size = aRange->ptr[1];
        aData->bytes = aRange->ptr + 2;
        aRange->ptr = aData->bytes + aData->size;
        return t;
    } else {
        aData->size = 0;
        aData->bytes = Q_NULLPTR;
        return 0;
    }
}

void
YubiKeyUtil::appendTLV(
    QByteArray* aTlv,
    uchar aTag,
    const QByteArray aValue)
{
    HASSERT(aValue.size() <= 0xff);
    aTlv->append((char)aTag);
    aTlv->append((char)aValue.size());
    aTlv->append(aValue);
}

void
YubiKeyUtil::appendTLV(
    QByteArray* aTlv,
    uchar aTag,
    uchar aLength,
    const void* aValue)
{
    aTlv->append((char)aTag);
    aTlv->append((char)aLength);
    aTlv->append((const char*)aValue, aLength);
}

const QString
YubiKeyUtil::algorithmName(
    YubiKeyAlgorithm aAlgorithm)
{
    switch (aAlgorithm) {
    case YubiKeyAlgorithm_Unknown: return QString();
    case YubiKeyAlgorithm_HMAC_SHA1: return ALGORITHM_SHA1;
    case YubiKeyAlgorithm_HMAC_SHA256: return ALGORITHM_SHA256;
    case YubiKeyAlgorithm_HMAC_SHA512: return ALGORITHM_SHA512;
    }
    HWARN("Unexpected/unknown algorithm" << aAlgorithm);
    return QString();
}

uchar
YubiKeyUtil::algorithmValue(
    YubiKeyAlgorithm aAlgorithm)
{
    switch (aAlgorithm) {
    case YubiKeyAlgorithm_Unknown: return 0;
    case YubiKeyAlgorithm_HMAC_SHA1: return YubiKeyConstants::ALG_HMAC_SHA1;
    case YubiKeyAlgorithm_HMAC_SHA256: return YubiKeyConstants::ALG_HMAC_SHA256;
    case YubiKeyAlgorithm_HMAC_SHA512: return YubiKeyConstants::ALG_HMAC_SHA512;
    }
    HWARN("Unexpected/unknown algorithm" << aAlgorithm);
    return 0;
}

YubiKeyAlgorithm
YubiKeyUtil::algorithmFromName(
    const QString aName)
{
    const QString name(aName.toUpper());

    return (name == ALGORITHM_SHA1) ? YubiKeyAlgorithm_HMAC_SHA1 :
        (name == ALGORITHM_SHA256) ? YubiKeyAlgorithm_HMAC_SHA256 :
        (name == ALGORITHM_SHA512) ? YubiKeyAlgorithm_HMAC_SHA512 :
        YubiKeyAlgorithm_Unknown;
}

YubiKeyAlgorithm
YubiKeyUtil::algorithmFromValue(
    uchar aValue)
{
    switch ((aValue & YubiKeyConstants::ALG_MASK)) {
    case YubiKeyConstants::ALG_HMAC_SHA1: return YubiKeyAlgorithm_HMAC_SHA1;
    case YubiKeyConstants::ALG_HMAC_SHA256: return YubiKeyAlgorithm_HMAC_SHA256;
    case YubiKeyConstants::ALG_HMAC_SHA512: return YubiKeyAlgorithm_HMAC_SHA512;
    }
    HWARN("Unknown digest value" << (uint)aValue);
    return YubiKeyAlgorithm_Unknown;
}

YubiKeyAlgorithm
YubiKeyUtil::validAlgorithm(
    int aValue)
{
    switch (aValue) {
    case YubiKeyAlgorithm_Unknown: break;
    case YubiKeyAlgorithm_HMAC_SHA1: // fallthrough
    case YubiKeyAlgorithm_HMAC_SHA256: // fallthrough
    case YubiKeyAlgorithm_HMAC_SHA512:
        return (YubiKeyAlgorithm)aValue;
    }
    return YubiKeyAlgorithm_Unknown;
}

YubiKeyTokenType
YubiKeyUtil::validType(
    int aValue)
{
    switch (aValue) {
    case YubiKeyTokenType_Unknown: break;
    case YubiKeyTokenType_HOTP: // fallthrough
    case YubiKeyTokenType_TOTP: // fallthrough
        return (YubiKeyTokenType)aValue;
    }
    return YubiKeyTokenType_Unknown;
}

bool
YubiKeyUtil::isValidBase32(
    const QString aBase32)
{
    return HarbourBase32::isValidBase32(aBase32);
}

QObject*
YubiKeyUtil::createSingleton(
    QQmlEngine*,
    QJSEngine*)
{
    return new YubiKeyUtil;
}

QDebug
operator<<(
    QDebug aDebug,
    YubiKeyTokenType aType)
{
    switch (aType) {
    case YubiKeyTokenType_Unknown: return (aDebug << "Unknown");
    case YubiKeyTokenType_HOTP: return (aDebug << "HOTP");
    case YubiKeyTokenType_TOTP: return (aDebug << "TOTP");
    }
    return (aDebug << (int)aType);
}

QDebug
operator<<(
    QDebug aDebug,
    YubiKeyAlgorithm aAlg)
{
    switch (aAlg) {
    case YubiKeyAlgorithm_Unknown: return (aDebug << "Unknown");
    case YubiKeyAlgorithm_HMAC_SHA1: return (aDebug << "HMAC-SHA1");
    case YubiKeyAlgorithm_HMAC_SHA256: return (aDebug << "HMAC-SHA256");
    case YubiKeyAlgorithm_HMAC_SHA512: return (aDebug << "HMAC-SHA512");
    }
    return (aDebug << (int)aAlg);
}

QDebug
operator<<(
    QDebug aDebug,
    YubiKeyAuthAccess aAccess)
{
    switch (aAccess) {
    case YubiKeyAuthAccessUnknown: return (aDebug << "AccessUnknown");
    case YubiKeyAuthAccessOpen: return (aDebug << "AccessOpen");
    case YubiKeyAuthAccessDenied: return (aDebug << "AccessDenied");
    case YubiKeyAuthAccessGranted: return (aDebug << "AccessGranted");
    }
    return (aDebug << (int)aAccess);
}
