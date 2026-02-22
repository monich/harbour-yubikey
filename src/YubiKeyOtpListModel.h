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

#ifndef _YUBIKEY_OTP_LIST_MODEL_H
#define _YUBIKEY_OTP_LIST_MODEL_H

#include "YubiKeyTypes.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>

class YubiKey;

class YubiKeyOtpListModel :
    public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(YubiKey* yubiKey READ yubiKey WRITE setYubiKey NOTIFY yubiKeyChanged)
    Q_PROPERTY(int favoriteTokenType READ favoriteTokenType NOTIFY favoriteTokenTypeChanged)
    Q_PROPERTY(bool favoriteMarkedForRefresh READ favoriteMarkedForRefresh NOTIFY favoriteMarkedForRefreshChanged)
    Q_PROPERTY(QString favoriteName READ favoriteName NOTIFY favoriteNameChanged)
    Q_PROPERTY(QString favoritePassword READ favoritePassword NOTIFY favoritePasswordChanged)
    Q_ENUMS(EntryOp)
    Q_ENUMS(EntryOpState)

public:
    enum EntryOp {
        EntryOpNone,
        EntryOpRefresh,
        EntryOpRename,
        EntryOpDelete
    };

    enum EntryOpState {
        EntryOpStateNone,
        EntryOpStateQueued,
        EntryOpStateActive,
        EntryOpStateFinished,
        EntryOpStateFailed
    };

    YubiKeyOtpListModel(QObject* aParent = Q_NULLPTR);
    ~YubiKeyOtpListModel();

    YubiKey* yubiKey() const;
    void setYubiKey(YubiKey*);

    YubiKeyTokenType favoriteTokenType() const;
    bool favoriteMarkedForRefresh() const;
    QString favoriteName() const;
    QString favoritePassword() const;

    Q_INVOKABLE bool containsName(QString) const;
    Q_INVOKABLE void deleteToken(QString);
    Q_INVOKABLE void refreshToken(QString);
    Q_INVOKABLE void renameToken(QString, QString);
    Q_INVOKABLE void cancelPendingOp(QString);

    // QAbstractItemModel
    Qt::ItemFlags flags(const QModelIndex&) const Q_DECL_OVERRIDE;
    QHash<int,QByteArray> roleNames() const Q_DECL_OVERRIDE;
    int rowCount(const QModelIndex&) const Q_DECL_OVERRIDE;
    QVariant data(const QModelIndex&, int) const Q_DECL_OVERRIDE;
    bool setData(const QModelIndex&, const QVariant&, int) Q_DECL_OVERRIDE;

Q_SIGNALS:
    void yubiKeyChanged();
    void favoriteTokenTypeChanged();
    void favoriteMarkedForRefreshChanged();
    void favoriteNameChanged();
    void favoritePasswordChanged();

private:
    class Entry;
    class Private;
    Private* iPrivate;
};

#endif // _YUBIKEY_OTP_LIST_MODEL_H
