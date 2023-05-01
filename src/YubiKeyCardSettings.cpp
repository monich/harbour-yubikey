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

#include "YubiKeyCardSettings.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"

#include <QAtomicInt>
#include <QSettings>

// ==========================================================================
// YubiKeyCardSettings::Private
// ==========================================================================

class YubiKeyCardSettings::Private :
    public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(Private)

    static QMap<QString,Private*> gMap;
    static const QChar LIST_SEPARATOR;
    static const QString SETTINGS_FILE;
    static const QString FAVORITE_ENTRY;
    static const QString STEAM_ENTRY;

    Private(const QString);
    ~Private();

public:
    static Private* get(const QString);
    static QString getFavoriteHash(QSettings*);

    Private* ref();
    void unref();

    bool isFavoriteName(const QString);
    void setFavoriteHash(const QString);
    void setSteamHashes(const QStringList);
    void addSteamHash(const QString);
    void removeSteamHash(const QString);
    void steamHashesUpdated();
    void tokenRenamed(const QString, const QString);

Q_SIGNALS:
    void favoriteHashChanged();
    void steamHashesChanged();

private:
    QAtomicInt iRef;
    QSettings* iSettings;

public:
    const QString iYubiKeyId;
    QString iFavoriteHash;
    QStringList iSteamHashes;
};

QMap<QString,YubiKeyCardSettings::Private*> YubiKeyCardSettings::Private::gMap;
const QChar YubiKeyCardSettings::Private::LIST_SEPARATOR(':');
const QString YubiKeyCardSettings::Private::SETTINGS_FILE("settings");
const QString YubiKeyCardSettings::Private::FAVORITE_ENTRY("Favorite");
const QString YubiKeyCardSettings::Private::STEAM_ENTRY("Steam");

YubiKeyCardSettings::Private::Private(
    const QString aYubiKeyId) :
    iRef(1),
    iSettings(Q_NULLPTR),
    iYubiKeyId(aYubiKeyId)
{
    // Add is to the static map
    HDEBUG(qPrintable(iYubiKeyId));
    HASSERT(!gMap.value(iYubiKeyId));
    gMap.insert(iYubiKeyId, this);

    if (!iYubiKeyId.isEmpty()) {
        QDir configDir(YubiKeyUtil::configDir());
        const QString settingsFile(configDir.absoluteFilePath(iYubiKeyId) +
            QDir::separator() + SETTINGS_FILE);

        HDEBUG("Settings" << qPrintable(settingsFile));
        configDir.mkpath(".");
        iSettings = new QSettings(settingsFile, QSettings::IniFormat);
        iFavoriteHash = getFavoriteHash(iSettings);
        iSteamHashes = iSettings->value(STEAM_ENTRY).toString().split(LIST_SEPARATOR);
    }
}

YubiKeyCardSettings::Private::~Private()
{
    // Remove it from the static map
    HDEBUG(qPrintable(iYubiKeyId));
    HASSERT(gMap.value(iYubiKeyId) == this);
    gMap.remove(iYubiKeyId);

    delete iSettings;
}

YubiKeyCardSettings::Private*
YubiKeyCardSettings::Private::get(
    const QString aYubiKeyId)
{
    Private* self = gMap.value(aYubiKeyId);
    if (self) {
        return self->ref();
    } else {
        return new Private(aYubiKeyId);
    }
}

YubiKeyCardSettings::Private*
YubiKeyCardSettings::Private::ref()
{
    iRef.ref();
    return this;
}

void
YubiKeyCardSettings::Private::unref()
{
    if (!iRef.deref()) {
        delete this;
    }
}

QString
YubiKeyCardSettings::Private::getFavoriteHash(
    QSettings* aSettings)
{
    if (aSettings) {
        QString favorite(aSettings->value(FAVORITE_ENTRY).toString());
        const int len = favorite.length();

        if (len > 0) {
            // Favorite name was originally stored in plain text
            static const int Sha1Len = 20 * 2; // 2 hex digits per byte
            bool isSha1 = false;

            if (len == Sha1Len) {
                isSha1 = true;
                const QChar* chars = favorite.constData();
                for (int i = 0; i < len && isSha1; i++) {
                    isSha1 = isxdigit(chars[i].unicode());
                }
            }
            if (isSha1) {
                return favorite;
            } else {
                QString hash(YubiKeyUtil::nameHash(favorite));

                HDEBUG("Replacing favorite name" << favorite << "with" << hash);
                aSettings->setValue(FAVORITE_ENTRY, hash);
                return hash;
            }
        }
    }
    return QString();
}

bool
YubiKeyCardSettings::Private::isFavoriteName(
    const QString aName)
{
    return !aName.isEmpty() && iFavoriteHash == YubiKeyUtil::nameHash(aName);
}

void
YubiKeyCardSettings::Private::setFavoriteHash(
    const QString aHash)
{
    if (iFavoriteHash != aHash) {
        iFavoriteHash = aHash;
        if (iSettings) {
            if (iFavoriteHash.isEmpty()) {
                iSettings->remove(FAVORITE_ENTRY);
            } else {
                iSettings->setValue(FAVORITE_ENTRY, iFavoriteHash);
            }
        }
        Q_EMIT favoriteHashChanged();
    }
}

void
YubiKeyCardSettings::Private::steamHashesUpdated()
{
    HDEBUG("Steam hashes" << iSteamHashes);
    if (iSettings) {
        if (iSteamHashes.isEmpty()) {
            iSettings->remove(STEAM_ENTRY);
        } else {
            iSettings->setValue(STEAM_ENTRY, iSteamHashes.join(LIST_SEPARATOR));
        }
    }
    Q_EMIT steamHashesChanged();
}

void
YubiKeyCardSettings::Private::setSteamHashes(
    const QStringList aList)
{
    if (iSteamHashes != aList) {
        iSteamHashes = aList;
        steamHashesUpdated();
    }
}

void
YubiKeyCardSettings::Private::addSteamHash(
    const QString aHash)
{
    if (!aHash.isEmpty() && !iSteamHashes.contains(aHash)) {
        iSteamHashes.append(aHash);
        iSteamHashes.sort();
        steamHashesUpdated();
    }
}

void
YubiKeyCardSettings::Private::removeSteamHash(
    const QString aHash)
{
    if (iSteamHashes.removeOne(aHash)) {
        steamHashesUpdated();
    }
}

void
YubiKeyCardSettings::Private::tokenRenamed(
    const QString aFrom,
    const QString aTo)
{
    if (aFrom != aTo) {
        if (isFavoriteName(aFrom)) {
            HDEBUG("Favorite token renamed");
            setFavoriteHash(aTo.isEmpty() ? QString() : YubiKeyUtil::nameHash(aTo));
        }
        if (iSteamHashes.removeOne(YubiKeyUtil::steamNameHash(aFrom))) {
            iSteamHashes.append(YubiKeyUtil::steamNameHash(aTo));
            iSteamHashes.sort();
            steamHashesUpdated();
        }
    }
}

// ==========================================================================
// YubiKeyCardSettings
// Acts as a wrapper around a shared YubiKeyCardSettings::Private object
// ==========================================================================

YubiKeyCardSettings::YubiKeyCardSettings(
    const QString aYubiKeyId,
    QObject* aParent) :
    QObject(aParent),
    iPrivate(Private::get(aYubiKeyId))
{
    connect(iPrivate, SIGNAL(favoriteHashChanged()), SIGNAL(favoriteHashChanged()));
    connect(iPrivate, SIGNAL(steamHashesChanged()), SIGNAL(steamHashesChanged()));
}

YubiKeyCardSettings::~YubiKeyCardSettings()
{
    iPrivate->disconnect(this);
    iPrivate->unref();
}

QString
YubiKeyCardSettings::favoriteHash() const
{
    return iPrivate->iFavoriteHash;
}

void
YubiKeyCardSettings::setFavoriteHash(
    const QString aHash)
{
    iPrivate->setFavoriteHash(aHash);
}

bool
YubiKeyCardSettings::isFavoriteName(
    const QString aName) const
{
    return iPrivate->isFavoriteName(aName);
}

void
YubiKeyCardSettings::setFavoriteName(
    const QString aName)
{
    iPrivate->setFavoriteHash(aName.isEmpty() ?
        QString() : YubiKeyUtil::nameHash(aName));
}

void
YubiKeyCardSettings::clearFavorite()
{
    iPrivate->setFavoriteHash(QString());
}

QStringList
YubiKeyCardSettings::steamHashes() const
{
    return iPrivate->iSteamHashes;
}

bool
YubiKeyCardSettings::isSteamHash(
    const QString aHash) const
{
    return iPrivate->iSteamHashes.contains(aHash);
}

void
YubiKeyCardSettings::setSteamHashes(
    const QStringList aHashes)
{
    iPrivate->setSteamHashes(aHashes);
}

void
YubiKeyCardSettings::addSteamHash(
    const QString aHash)
{
    iPrivate->addSteamHash(aHash);
}

void
YubiKeyCardSettings::removeSteamHash(
    const QString aHash)
{
    iPrivate->removeSteamHash(aHash);
}

void
YubiKeyCardSettings::tokenRenamed(
    const QString aFrom,
    const QString aTo)
{
    iPrivate->tokenRenamed(aFrom, aTo);
}

#include "YubiKeyCardSettings.moc"
