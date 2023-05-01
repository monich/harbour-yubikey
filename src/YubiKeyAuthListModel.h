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

#ifndef _YUBIKEY_AUTH_LIST_MODEL_H
#define _YUBIKEY_AUTH_LIST_MODEL_H

#include <YubiKeyTypes.h>

#include <QStringList>
#include <QAbstractListModel>

class YubiKeyAuthListModel :
    public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY(YubiKeyAuthListModel)
    Q_PROPERTY(QString yubiKeyId READ yubiKeyId WRITE setYubiKeyId NOTIFY yubiKeyIdChanged)
    Q_PROPERTY(int favoriteTokenType READ favoriteTokenType NOTIFY favoriteTokenTypeChanged)
    Q_PROPERTY(bool favoriteMarkedForRefresh READ favoriteMarkedForRefresh NOTIFY favoriteMarkedForRefreshChanged)
    Q_PROPERTY(bool favoritePasswordExpired READ favoritePasswordExpired NOTIFY favoritePasswordExpiredChanged)
    Q_PROPERTY(QString favoriteName READ favoriteName NOTIFY favoriteNameChanged)
    Q_PROPERTY(QString favoritePassword READ favoritePassword NOTIFY favoritePasswordChanged)
    Q_PROPERTY(QString authList READ authList WRITE setAuthList NOTIFY authListChanged)
    Q_PROPERTY(QString authData READ authData WRITE setAuthData NOTIFY authDataChanged)
    Q_PROPERTY(QStringList refreshableTokens READ refreshableTokens WRITE setRefreshableTokens NOTIFY refreshableTokensChanged)
    Q_PROPERTY(QStringList markedForRefresh READ markedForRefresh NOTIFY markedForRefreshChanged)
    Q_PROPERTY(QStringList markedForDeletion READ markedForDeletion NOTIFY markedForDeletionChanged)
    Q_PROPERTY(bool haveExpiringTotpCodes READ haveExpiringTotpCodes NOTIFY haveExpiringTotpCodesChanged)

public:
    YubiKeyAuthListModel(QObject* aParent = Q_NULLPTR);
    ~YubiKeyAuthListModel();

    void setYubiKeyId(const QString);
    QString yubiKeyId() const;

    QString authList() const;
    void setAuthList(const QString);

    QString authData() const;
    void setAuthData(const QString);

    QStringList refreshableTokens() const;
    void setRefreshableTokens(const QStringList);

    YubiKeyTokenType favoriteTokenType() const;
    bool favoriteMarkedForRefresh() const;
    bool favoritePasswordExpired() const;
    QString favoriteName() const;
    QString favoritePassword() const;
    QStringList markedForRefresh() const;
    QStringList markedForDeletion() const;
    bool haveExpiringTotpCodes() const;

    Q_INVOKABLE bool containsName(const QString) const;

    // QAbstractItemModel
    Qt::ItemFlags flags(const QModelIndex&) const Q_DECL_OVERRIDE;
    QHash<int,QByteArray> roleNames() const Q_DECL_OVERRIDE;
    int rowCount(const QModelIndex& aParent = QModelIndex()) const Q_DECL_OVERRIDE;
    QVariant data(const QModelIndex&, int) const Q_DECL_OVERRIDE;
    bool setData(const QModelIndex&, const QVariant&, int) Q_DECL_OVERRIDE;

public Q_SLOTS:
    void totpCodesExpired();
    void tokenRenamed(const QString, const QString);

Q_SIGNALS:
    void yubiKeyIdChanged();
    void favoriteTokenTypeChanged();
    void favoriteMarkedForRefreshChanged();
    void favoritePasswordExpiredChanged();
    void favoriteNameChanged();
    void favoritePasswordChanged();
    void authListChanged();
    void authDataChanged();
    void refreshableTokensChanged();
    void markedForRefreshChanged();
    void markedForDeletionChanged();
    void haveExpiringTotpCodesChanged();

private:
    class ModelData;
    class Private;
    Private* iPrivate;
};

#endif // _YUBIKEY_AUTH_LIST_MODEL_H
