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

#ifndef _YUBIKEY_H
#define _YUBIKEY_H

#include "YubiKeyToken.h"

#include <QByteArray>
#include <QDebug>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class YubiKey :
    public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(YubiKey)

private:
    YubiKey(const QByteArray);
    ~YubiKey();

public:
    static YubiKey* get(const QByteArray);
    void put() { unref(); }

    YubiKey* ref();
    void unref();

    const QByteArray yubiKeyId() const;
    const QByteArray otpList() const;
    const QByteArray otpData() const;
    bool otpListFetched() const;
    bool present() const;
    YubiKeyAuthAccess authAccess() const;
    uint yubiKeySerial() const;
    uint yubiKeyVersion() const;

    const QString yubiKeyIdString() const;
    const QString yubiKeyVersionString() const;
    const QString otpListString() const;
    const QString otpDataString() const;
    const QStringList refreshableTokens() const;
    const QList<int> operationIds() const;
    bool totpValid() const;
    int totpTimeLeft() const; // seconds

    int putTokens(const QList<YubiKeyToken>);
    int renameToken(const QString, const QString);
    bool submitPassword(const QString, bool);
    void refreshTokens(const QStringList);
    void deleteTokens(const QStringList);
    int setPassword(const QString);
    int reset();

Q_SIGNALS:
    void yubiKeySerialChanged();
    void yubiKeyVersionChanged();
    void otpListFetchedChanged();
    void otpListChanged();
    void otpDataChanged();
    void presentChanged();
    void authAccessChanged();
    void refreshableTokensChanged();
    void totpValidChanged();
    void totpTimeLeftChanged();
    void operationIdsChanged();
    void operationFinished(int, bool);
    void accessKeyNotAccepted();
    void totpCodesExpired();
    void passwordChanged();
    void passwordRemoved();
    void tokenRenamed(const QString, const QString);
    void yubiKeyReset();

private:
    class Private;
    Private* iPrivate;
};

#endif // _YUBIKEY_H
