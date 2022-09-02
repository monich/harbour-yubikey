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

#ifndef _YUBIKEY_CARD_H
#define _YUBIKEY_CARD_H

#include "YubiKeyConstants.h"
#include "YubiKeyToken.h"

#include <QObject>
#include <QString>
#include <QStringList>

class YubiKeyCard :
    public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(YubiKeyCard)
    Q_PROPERTY(QString yubiKeyId READ yubiKeyId WRITE setYubiKeyId NOTIFY yubiKeyIdChanged)
    Q_PROPERTY(uint yubiKeySerial READ yubiKeySerial NOTIFY yubiKeySerialChanged)
    Q_PROPERTY(QString yubiKeyVersion READ yubiKeyVersion NOTIFY yubiKeyVersionChanged)
    Q_PROPERTY(QString yubiKeyOtpList READ yubiKeyOtpList NOTIFY yubiKeyOtpListChanged)
    Q_PROPERTY(QString yubiKeyOtpData READ yubiKeyOtpData NOTIFY yubiKeyOtpDataChanged)
    Q_PROPERTY(YubiKeyState yubiKeyState READ yubiKeyState NOTIFY yubiKeyStateChanged)
    Q_PROPERTY(AuthAccess authAccess READ authAccess NOTIFY authAccessChanged)
    Q_PROPERTY(QStringList refreshableTokens READ refreshableTokens NOTIFY refreshableTokensChanged)
    Q_PROPERTY(QList<int> operationIds READ operationIds NOTIFY operationIdsChanged)
    Q_PROPERTY(bool present READ present NOTIFY presentChanged)
    Q_PROPERTY(bool otpListFetched READ otpListFetched NOTIFY otpListFetchedChanged)
    Q_PROPERTY(bool totpValid READ totpValid NOTIFY totpValidChanged)
    Q_PROPERTY(qreal totpTimeLeft READ totpTimeLeft NOTIFY totpTimeLeftChanged)
    Q_ENUMS(AuthAccess)
    Q_ENUMS(YubiKeyState)
    Q_ENUMS(Constants)

public:
    enum Constants {
        TotpPeriod = YubiKeyConstants::TOTP_PERIOD_SEC,

        HMAC_SHA1 = YubiKeyAlgorithm_HMAC_SHA1,
        HMAC_SHA256 = YubiKeyAlgorithm_HMAC_SHA256,
        HMAC_SHA512 = YubiKeyAlgorithm_HMAC_SHA512,

        TypeUnknown = YubiKeyTokenType_Unknown,
        TypeHOTP = YubiKeyTokenType_HOTP,
        TypeTOTP = YubiKeyTokenType_TOTP
    };

    enum YubiKeyState {
        YubiKeyStateIdle,           // Wrong YubiKey (or none at all)
        YubiKeyStateUnauthorized,   // We don't know the authorization code
        YubiKeyStateReady           // YubiKey is detected and ready
    };

    enum AuthAccess {
        AccessUnknown,  // Unknown state
        AccessOpen,     // Authentication disabled
        AccessDenied,   // Successfully authenticated
        AccessGranted   // Not authenticated
    };

    YubiKeyCard(QObject* aParent = Q_NULLPTR);
    ~YubiKeyCard();

    void setYubiKeyId(const QString);
    QString yubiKeyId() const;
    uint yubiKeySerial() const;
    QString yubiKeyVersion() const;
    QString yubiKeyOtpList() const;
    QString yubiKeyOtpData() const;
    YubiKeyState yubiKeyState() const;
    AuthAccess authAccess() const;
    QStringList refreshableTokens() const;
    QList<int> operationIds() const;
    bool present() const;
    bool otpListFetched() const;
    bool totpValid() const;
    qreal totpTimeLeft() const;

    Q_INVOKABLE bool validOperationId(int);
    Q_INVOKABLE int putToken(int, int, const QString, const QString, int, int);
    Q_INVOKABLE int putTokens(const QList<YubiKeyToken>);
    Q_INVOKABLE void refreshTokens(const QStringList);
    Q_INVOKABLE void deleteTokens(const QStringList);
    Q_INVOKABLE bool submitPassword(const QString, bool);
    Q_INVOKABLE int setPassword(const QString);
    Q_INVOKABLE int reset();

Q_SIGNALS:
    void yubiKeyReset();
    void yubiKeyIdChanged();
    void yubiKeySerialChanged();
    void yubiKeyVersionChanged();
    void yubiKeyOtpListChanged();
    void yubiKeyOtpDataChanged();
    void yubiKeyStateChanged();
    void authAccessChanged();
    void refreshableTokensChanged();
    void operationIdsChanged();
    void presentChanged();
    void otpListFetchedChanged();
    void totpValidChanged();
    void totpTimeLeftChanged();
    void totpCodesExpired();
    void accessKeyNotAccepted();
    void passwordChanged();
    void passwordRemoved();
    void operationFinished(int operationId, bool success);

private:
    class Private;
    Private* iPrivate;
};

#endif // _YUBIKEY_CARD_H
