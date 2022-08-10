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

#include "YubiKeyToken.h"

#include "HarbourBase32.h"
#include "HarbourDebug.h"

#include <QAtomicInt>

// ==========================================================================
// YubiKeyToken::Private
// ==========================================================================

class YubiKeyToken::Private
{
public:
    Private(YubiKeyTokenType, YubiKeyAlgorithm, const QString, const QString,
        const QByteArray, const QString, int, int);
    ~Private();

public:
    QAtomicInt iRef;
    YubiKeyTokenType iType;
    YubiKeyAlgorithm iAlgorithm;
    QString iLabel;
    QString iIssuer;
    QByteArray iSecret;
    QString iSecretBase32;
    int iDigits;
    int iCounter;
};

YubiKeyToken::Private::Private(
    YubiKeyTokenType aType,
    YubiKeyAlgorithm aAlgorithm,
    const QString aLabel,
    const QString aIssuer,
    const QByteArray aSecret,
    const QString aSecretBase32,
    int aDigits,
    int aCounter) :
    iRef(1),
    iType(aType),
    iAlgorithm(aAlgorithm),
    iLabel(aLabel),
    iIssuer(aIssuer),
    iSecret(aSecret),
    iSecretBase32(aSecretBase32),
    iDigits(aDigits),
    iCounter(aCounter)
{
}

YubiKeyToken::Private::~Private()
{
}

// ==========================================================================
// YubiKeyToken
// ==========================================================================

const QString YubiKeyToken::KEY_VALID("valid");
const QString YubiKeyToken::KEY_TYPE("type");
const QString YubiKeyToken::KEY_ALGORITHM("algorithm");
const QString YubiKeyToken::KEY_LABEL("label");
const QString YubiKeyToken::KEY_SECRET("secret");
const QString YubiKeyToken::KEY_ISSUER("issuer");
const QString YubiKeyToken::KEY_DIGITS("digits");
const QString YubiKeyToken::KEY_COUNTER("counter");

YubiKeyToken::YubiKeyToken(
    YubiKeyTokenType aType,
    YubiKeyAlgorithm aAlgorithm,
    const QString aLabel,
    const QString aIssuer,
    const QByteArray aSecret,
    int aDigits,
    int aCounter) :
    iPrivate(new Private(aType, aAlgorithm, aLabel, aIssuer, aSecret,
        HarbourBase32::toBase32(aSecret, true), aDigits, aCounter))
{
}

YubiKeyToken::YubiKeyToken(
    const YubiKeyToken& aToken) :
    iPrivate(aToken.iPrivate)
{
    if (iPrivate) {
        iPrivate->iRef.ref();
    }
}

YubiKeyToken::YubiKeyToken(
    Private* aPrivate) :
    iPrivate(aPrivate)
{
}

YubiKeyToken::YubiKeyToken() :
    iPrivate(NULL)
{
}

YubiKeyToken::~YubiKeyToken()
{
    if (iPrivate && !iPrivate->iRef.deref()) {
        delete iPrivate;
    }
}

YubiKeyToken&
YubiKeyToken::operator=(
    const YubiKeyToken& aToken)
{
    if (iPrivate != aToken.iPrivate) {
        if (iPrivate && !iPrivate->iRef.deref()) {
            delete iPrivate;
        }
        iPrivate = aToken.iPrivate;
        if (iPrivate) {
            iPrivate->iRef.ref();
        }
    }
    return *this;
}

bool
YubiKeyToken::equals(
    const YubiKeyToken& aToken) const
{
    if (iPrivate == aToken.iPrivate) {
        return true;
    } else if (iPrivate && aToken.iPrivate) {
        const Private* other = aToken.iPrivate;

        return iPrivate->iType == other->iType &&
            iPrivate->iAlgorithm == other->iAlgorithm &&
            iPrivate->iDigits == other->iDigits &&
            iPrivate->iCounter == other->iCounter &&
            iPrivate->iLabel == other->iLabel &&
            iPrivate->iIssuer == other->iIssuer &&
            iPrivate->iSecret == other->iSecret;
    } else {
        return false;
    }
}

bool
YubiKeyToken::valid() const
{
    return iPrivate != Q_NULLPTR;
}

YubiKeyTokenType
YubiKeyToken::type() const
{
    return iPrivate ? iPrivate->iType : YubiKeyTokenType_Unknown;
}

YubiKeyAlgorithm
YubiKeyToken::algorithm() const
{
    return iPrivate ? iPrivate->iAlgorithm : YubiKeyAlgorithm_Unknown;
}

QString
YubiKeyToken::label() const
{
    return iPrivate ? iPrivate->iLabel : QString();
}

QString
YubiKeyToken::issuer() const
{
    return iPrivate ? iPrivate->iIssuer : QString();
}

QString
YubiKeyToken::secretBase32() const
{
    return iPrivate ? iPrivate->iSecretBase32 : QString();
}

const QByteArray
YubiKeyToken::secret() const
{
    return iPrivate ? iPrivate->iSecret : QByteArray();
}

int
YubiKeyToken::digits() const
{
    return iPrivate ? iPrivate->iDigits : 0;
}

int
YubiKeyToken::counter() const
{
    return iPrivate ? iPrivate->iCounter : 0;
}

bool
YubiKeyToken::operator==(
    const YubiKeyToken& aToken) const
{
    return equals(aToken);
}

bool
YubiKeyToken::operator!=(
    const YubiKeyToken& aToken) const
{
    return !equals(aToken);
}

QVariantMap
YubiKeyToken::toVariantMap() const
{
    QVariantMap map;

    if (iPrivate) {
        map.insert(KEY_VALID, true);
        map.insert(KEY_TYPE, int(iPrivate->iType));
        map.insert(KEY_ALGORITHM, int(iPrivate->iAlgorithm));
        map.insert(KEY_LABEL, iPrivate->iLabel);
        map.insert(KEY_ISSUER, iPrivate->iIssuer);
        map.insert(KEY_SECRET, iPrivate->iSecretBase32);
        map.insert(KEY_DIGITS, iPrivate->iDigits);
        map.insert(KEY_COUNTER, iPrivate->iCounter);
    }
    return map;
}

YubiKeyToken
YubiKeyToken::withType(
    YubiKeyTokenType aType) const
{
    if (!iPrivate) {
        return YubiKeyToken();
    } else if (iPrivate->iType == aType) {
        return *this;
    } else {
        return YubiKeyToken(new Private(aType, iPrivate->iAlgorithm,
            iPrivate->iLabel, iPrivate->iIssuer, iPrivate->iSecret,
            iPrivate->iSecretBase32, iPrivate->iDigits, iPrivate->iCounter));
    }
}

YubiKeyToken
YubiKeyToken::withAlgorithm(
    YubiKeyAlgorithm aAlgorithm) const
{
    if (!iPrivate) {
        return YubiKeyToken();
    } else if (iPrivate->iAlgorithm == aAlgorithm) {
        return *this;
    } else {
        return YubiKeyToken(new Private(iPrivate->iType, aAlgorithm,
            iPrivate->iLabel, iPrivate->iIssuer, iPrivate->iSecret,
            iPrivate->iSecretBase32, iPrivate->iDigits, iPrivate->iCounter));
    }
}

YubiKeyToken
YubiKeyToken::withLabel(
    const QString aLabel) const
{
    if (!iPrivate) {
        return YubiKeyToken();
    } else if (iPrivate->iLabel == aLabel) {
        return *this;
    } else {
        return YubiKeyToken(new Private(iPrivate->iType, iPrivate->iAlgorithm,
            aLabel, iPrivate->iIssuer, iPrivate->iSecret,
            iPrivate->iSecretBase32, iPrivate->iDigits, iPrivate->iCounter));
    }
}

YubiKeyToken
YubiKeyToken::withIssuer(
    const QString aIssuer) const
{
    if (!iPrivate) {
        return YubiKeyToken();
    } else if (iPrivate->iIssuer == aIssuer) {
        return *this;
    } else {
        return YubiKeyToken(new Private(iPrivate->iType, iPrivate->iAlgorithm,
            iPrivate->iLabel, aIssuer, iPrivate->iSecret,
            iPrivate->iSecretBase32, iPrivate->iDigits, iPrivate->iCounter));
    }
}

YubiKeyToken
YubiKeyToken::withSecret(
    const QByteArray aSecret) const
{
    if (!iPrivate) {
        return YubiKeyToken();
    } else if (iPrivate->iSecret == aSecret) {
        return *this;
    } else {
        return YubiKeyToken(new Private(iPrivate->iType, iPrivate->iAlgorithm,
            iPrivate->iLabel, iPrivate->iIssuer, aSecret,
            HarbourBase32::toBase32(aSecret, true), iPrivate->iDigits,
            iPrivate->iCounter));
    }

}

YubiKeyToken
YubiKeyToken::withSecretBase32(
    const QString aBase32) const
{
    if (!iPrivate) {
        return YubiKeyToken();
    } else if (!HarbourBase32::isValidBase32(aBase32)) {
        return *this;
    }  else {
        const QByteArray secret(HarbourBase32::fromBase32(aBase32));

        if (iPrivate->iSecret == secret) {
            return *this;
        } else {
            return YubiKeyToken(new Private(iPrivate->iType, iPrivate->iAlgorithm,
                iPrivate->iLabel, iPrivate->iIssuer, secret,
                HarbourBase32::toBase32(secret, true), iPrivate->iDigits,
                iPrivate->iCounter));
        }
    }
}

YubiKeyToken
YubiKeyToken::withDigits(
    int aDigits) const
{
    if (!iPrivate) {
        return YubiKeyToken();
    } else if (iPrivate->iDigits == aDigits) {
        return *this;
    } else {
        return YubiKeyToken(new Private(iPrivate->iType, iPrivate->iAlgorithm,
            iPrivate->iLabel, iPrivate->iIssuer, iPrivate->iSecret,
            iPrivate->iSecretBase32, aDigits, iPrivate->iCounter));
    }
}

YubiKeyToken
YubiKeyToken::withCounter(
    int aCounter) const
{
    if (!iPrivate) {
        return YubiKeyToken();
    } else if (iPrivate->iCounter == aCounter) {
        return *this;
    } else {
        return YubiKeyToken(new Private(iPrivate->iType, iPrivate->iAlgorithm,
            iPrivate->iLabel, iPrivate->iIssuer, iPrivate->iSecret,
            iPrivate->iSecretBase32, iPrivate->iDigits, aCounter));
    }
}

#if HARBOUR_DEBUG
QDebug
operator<<(
    QDebug aDebug,
    const YubiKeyToken& aToken)
{
    if (aToken.valid()) {
        QDebugStateSaver saver(aDebug);
        aDebug.nospace() << "YubiKeyToken(" << aToken.label() <<
            ", " << aToken.issuer() << ", " <<aToken.type() <<
            ", " << aToken.algorithm() << ", " <<  aToken.secretBase32() <<
            ", " << aToken.digits() << ", " << aToken.counter() << ")";
    } else {
        aDebug << "YubiKeyToken()";
    }
    return aDebug;
}
#endif // HARBOUR_DEBUG
