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

#ifndef _YUBIKEY_H
#define _YUBIKEY_H

#include "YubiKeyConstants.h"
#include "YubiKeyOp.h"
#include "YubiKeyOtp.h"
#include "YubiKeyToken.h"

#include <QtCore/QList>
#include <QtCore/QObject>

class YubiKeyIo;

class YubiKey :
    public QObject
{
    Q_OBJECT
    Q_PROPERTY(YubiKeyIo* yubiKeyIo READ yubiKeyIo WRITE setYubiKeyIo NOTIFY yubiKeyIoChanged)
    Q_PROPERTY(QString yubiKeyId READ yubiKeyIdString NOTIFY yubiKeyIdChanged)
    Q_PROPERTY(uint yubiKeySerial READ yubiKeySerial NOTIFY yubiKeySerialChanged)
    Q_PROPERTY(uint yubiKeyVersion READ yubiKeyVersion NOTIFY yubiKeyVersionChanged)
    Q_PROPERTY(QString yubiKeyVersionString READ yubiKeyVersionString NOTIFY yubiKeyVersionChanged)
    Q_PROPERTY(AuthAccess authAccess READ authAccess NOTIFY authAccessChanged)
    Q_PROPERTY(Transport transport READ transport NOTIFY transportChanged)
    Q_PROPERTY(QList<YubiKeyOtp> otpList READ otpList NOTIFY otpListChanged)
    Q_PROPERTY(bool otpListFetched READ otpListFetched NOTIFY otpListFetchedChanged)
    Q_PROPERTY(bool present READ present NOTIFY presentChanged)
    Q_PROPERTY(bool updatingPasswords READ updatingPasswords NOTIFY updatingPasswordsChanged)
    Q_PROPERTY(bool haveTotpCodes READ haveTotpCodes NOTIFY haveTotpCodesChanged)
    Q_PROPERTY(bool haveBeenReset READ haveBeenReset NOTIFY haveBeenResetChanged)
    Q_PROPERTY(qreal totpTimeLeft READ totpTimeLeft NOTIFY totpTimeLeftChanged)
    Q_ENUMS(AuthAccess)
    Q_ENUMS(Constants)
    Q_ENUMS(Transport)

public:
    enum Constants {
        TotpPeriod = YubiKeyConstants::TOTP_PERIOD_SEC,
        DefaultDigits = YubiKeyToken::DefaultDigits,
        MinDigits = YubiKeyToken::MinDigits,
        MaxDigits = YubiKeyToken::MaxDigits,

        HMAC_SHA1 = YubiKeyAlgorithm_HMAC_SHA1,
        HMAC_SHA256 = YubiKeyAlgorithm_HMAC_SHA256,
        HMAC_SHA512 = YubiKeyAlgorithm_HMAC_SHA512,

        TypeUnknown = YubiKeyTokenType_Unknown,
        TypeHOTP = YubiKeyTokenType_HOTP,
        TypeTOTP = YubiKeyTokenType_TOTP,

        // YubiKey firmware versions
        Version_5_3_0 = 0x050300,

        // Error codes
        Success = YubiKeyConstants::RC_OK,
        ErrorNoSpace = YubiKeyConstants::RC_NO_SPACE
    };

    enum Transport {
        TransportUnknown,
        TransportNFC,
        TransportUSB
    };

    enum AuthAccess {
        AccessUnknown = YubiKeyAuthAccessUnknown,
        AccessNotActivated = YubiKeyAuthAccessNotActivated,
        AccessOpen = YubiKeyAuthAccessOpen,
        AccessGranted = YubiKeyAuthAccessGranted,
        AccessDenied = YubiKeyAuthAccessDenied
    };

    YubiKey(QObject* aParent = Q_NULLPTR);
    ~YubiKey();

    YubiKeyIo* yubiKeyIo() const;
    void setYubiKeyIo(YubiKeyIo*);

    QByteArray yubiKeyId() const;
    QString yubiKeyIdString() const;
    uint yubiKeySerial() const;
    uint yubiKeyVersion() const;
    QString yubiKeyVersionString() const;
    Transport transport() const;
    AuthAccess authAccess() const;
    QList<YubiKeyOtp> otpList() const;
    bool otpListFetched() const;
    bool present() const;
    bool updatingPasswords() const;
    bool haveTotpCodes() const;
    bool haveBeenReset() const;
    qreal totpTimeLeft() const;

    Q_INVOKABLE void clear();
    Q_INVOKABLE void authorize(QString, bool);
    Q_INVOKABLE bool cancelOp(int);
    Q_INVOKABLE YubiKeyOp* getOp(int);
    Q_INVOKABLE YubiKeyOp* reset();
    Q_INVOKABLE YubiKeyOp* clearPassword();
    Q_INVOKABLE YubiKeyOp* setPassword(QString);
    Q_INVOKABLE YubiKeyOp* putToken(int, int, const QString, const QString, int, int);
    Q_INVOKABLE QList<int> putTokens(QList<YubiKeyToken>);

    YubiKeyOp* refreshToken(QByteArray);
    YubiKeyOp* deleteToken(QByteArray);
    YubiKeyOp* renameToken(QByteArray, QByteArray);
    void listAndCalculateAll();

Q_SIGNALS:
    void yubiKeyIoChanged();
    void yubiKeyIdChanged();
    void yubiKeySerialChanged();
    void yubiKeyVersionChanged();
    void transportChanged();
    void authAccessChanged();
    void otpListChanged();
    void otpListFetchedChanged();
    void presentChanged();
    void updatingPasswordsChanged();
    void haveTotpCodesChanged();
    void haveBeenResetChanged();
    void totpTimeLeftChanged();
    void yubiKeyConnected();
    void yubiKeyValidationFailed();
    void invalidYubiKeyConnected();
    void restrictedYubiKeyConnected();

private:
    class Private;
    Private* iPrivate;
};

#endif // _YUBIKEY_H
