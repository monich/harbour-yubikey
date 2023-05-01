/*
 * Copyright (C) 2023 Slava Monich <slava@monich.com>
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

#ifndef _YUBIKEY_CARD_SETTINGS_H
#define _YUBIKEY_CARD_SETTINGS_H

#include <QObject>
#include <QStringList>

class YubiKeyCardSettings :
    public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(YubiKeyCardSettings)

public:
    YubiKeyCardSettings(const QString, QObject* aParent = Q_NULLPTR);
    ~YubiKeyCardSettings();

    QString favoriteHash() const;
    void setFavoriteHash(const QString);
    bool isFavoriteName(const QString) const;
    void setFavoriteName(const QString);
    void clearFavorite();

    QStringList steamHashes() const;
    bool isSteamHash(const QString) const;
    void setSteamHashes(const QStringList);
    void addSteamHash(const QString);
    void removeSteamHash(const QString);

    void tokenRenamed(const QString, const QString);

Q_SIGNALS:
    void favoriteHashChanged();
    void steamHashesChanged();

private:
    class Private;
    Private* iPrivate;
};

#endif // _YUBIKEY_CARD_SETTINGS_H
