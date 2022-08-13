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

#ifndef _YUBIKEY_TOKEN_H
#define _YUBIKEY_TOKEN_H

#include "YubiKeyTypes.h"

#include <QByteArray>
#include <QDebug>
#include <QMetaType>
#include <QString>

class YubiKeyToken
{
    Q_GADGET
    Q_PROPERTY(bool valid READ valid CONSTANT)
    Q_PROPERTY(int type READ type CONSTANT)
    Q_PROPERTY(int algorithm READ algorithm CONSTANT)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(QString issuer READ issuer CONSTANT)
    Q_PROPERTY(QString secret READ secretBase32 CONSTANT)
    Q_PROPERTY(int digits READ digits CONSTANT)
    Q_PROPERTY(int counter READ counter CONSTANT)

public:
    enum Constants {
        DefaultDigits = 6,
        MinDigits = 6,
        MaxDigits = 8
    };

    YubiKeyToken();
    YubiKeyToken(const YubiKeyToken&);
    YubiKeyToken(YubiKeyTokenType, YubiKeyAlgorithm, const QString,
        const QString, const QByteArray, int, int);
    ~YubiKeyToken();

    YubiKeyToken& operator = (const YubiKeyToken&);
    bool operator == (const YubiKeyToken&) const;
    bool operator != (const YubiKeyToken&) const;
    bool equals(const YubiKeyToken&) const;

    bool valid() const;
    YubiKeyTokenType type() const;
    YubiKeyAlgorithm algorithm() const;
    QString label() const;
    QString issuer() const;
    QString secretBase32() const;
    const QByteArray secret() const;
    int digits() const;
    int counter() const;

    Q_REQUIRED_RESULT YubiKeyToken withType(YubiKeyTokenType) const;
    Q_REQUIRED_RESULT YubiKeyToken withAlgorithm(YubiKeyAlgorithm) const;
    Q_REQUIRED_RESULT YubiKeyToken withLabel(const QString) const;
    Q_REQUIRED_RESULT YubiKeyToken withIssuer(const QString) const;
    Q_REQUIRED_RESULT YubiKeyToken withSecret(const QByteArray) const;
    Q_REQUIRED_RESULT YubiKeyToken withSecretBase32(const QString) const;
    Q_REQUIRED_RESULT YubiKeyToken withDigits(int) const;
    Q_REQUIRED_RESULT YubiKeyToken withCounter(int) const;

private:
    class Private;
    YubiKeyToken(Private*);
    Private* iPrivate;
};

// Debug output
QDebug operator<<(QDebug, const YubiKeyToken&);

Q_DECLARE_METATYPE(YubiKeyToken)

#endif // _YUBIKEY_TOKEN_H
