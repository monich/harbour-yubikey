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
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#ifndef _YUBIKEY_UTIL_H
#define _YUBIKEY_UTIL_H

#include "foil_types.h"

#include "YubiKeyToken.h"

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QObject>

class QQmlEngine;
class QJSEngine;

class YubiKeyUtil:
    public QObject
{
    Q_OBJECT
    Q_ENUMS(Constants)
    YubiKeyUtil() : QObject(Q_NULLPTR) {}

public:
    enum Constants {
        // To expose these to QML:
        DefaultDigits = YubiKeyToken::DefaultDigits,
        MinDigits = YubiKeyToken::MinDigits,
        MaxDigits = YubiKeyToken::MaxDigits
    };

    static const QString ALGORITHM_SHA1;
    static const QString ALGORITHM_SHA256;
    static const QString ALGORITHM_SHA512;

    static QDir configDir();
    static QDir configDir(const QByteArray);

    static QString nameHashUtf8(const QByteArray&);
    static QString nameHash(const QString& aName)
        { return nameHashUtf8(aName.toUtf8()); }

    static uint versionToNumber(const uchar*, gsize);
    static inline uint versionToNumber(const QByteArray& aData)
        { return versionToNumber((uchar*)aData.constData(), aData.size()); }

    static QString versionToString(const uchar*, gsize);
    static inline QString versionToString(const QByteArray& aData)
        { return versionToString((uchar*)aData.constData(), aData.size()); }

    static const FoilBytes* fromByteArray(FoilBytes*, const QByteArray&);
    static QByteArray toByteArray(const GUtilData*);
    static QByteArray toByteArray(GBytes*, bool aDropBytes = true);
    static QByteArray fromHex(const QString);

    static QString toHex(const uchar*, gsize);
    static inline QString toHex(const QByteArray& aData)
        { return toHex((uchar*)aData.constData(), aData.size()); }
    static inline QString toHex(const GUtilData* aData)
        { return toHex(aData->bytes, aData->size); }

    static uchar readTLV(GUtilRange*, GUtilData*);
    static void appendTLV(QByteArray*, uchar, const QByteArray);
    static void appendTLV(QByteArray*, uchar, uchar, const void*);

    static const QString algorithmName(YubiKeyAlgorithm);
    static uchar algorithmValue(YubiKeyAlgorithm);
    static YubiKeyAlgorithm algorithmFromName(const QString);
    static YubiKeyAlgorithm algorithmFromValue(uchar);
    static YubiKeyAlgorithm validAlgorithm(int);

    static YubiKeyTokenType validType(int);

    Q_INVOKABLE static bool isValidBase32(const QString);

    // Callback for qmlRegisterSingletonType<YubiKeyUtil>
    static QObject* createSingleton(QQmlEngine*, QJSEngine*);
};

#endif // _YUBIKEY_UTIL_H
