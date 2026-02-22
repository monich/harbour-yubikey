/*
 * Copyright (C) 2023-2026 Slava Monich <slava@monich.com>
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

#include "YubiKeySettings.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"

#include <QtCore/QAtomicInt>
#include <QtCore/QDir>
#include <QtCore/QSettings>
#include <QtCore/QStringList>

// ==========================================================================
// YubiKeySettings::Private
// ==========================================================================

class YubiKeySettings::Private :
    public QObject
{
    Q_OBJECT

    static const QString SETTINGS_FILE;
    static const QString FAVORITE_ENTRY;
    static const QString STEAM_ENTRY;
    static const QChar LIST_SEPARATOR;

public:
    Private(const QByteArray&);
    ~Private();

    static QByteArray getFavoriteHash(QSettings*);

    void connect(YubiKeySettings*);
    bool isFavoriteName(const QString&);
    void setFavoriteHash(const QByteArray&);
    void setFavoriteName(const QString&);
    void setSteamHashes(const QByteArrayList&);
    void addSteamHash(const QByteArray&);
    void removeSteamHash(const QByteArray&);
    void steamHashesUpdated();
    void tokenRenamed(const QString&, const QString&);

    void update(const QString&, const QVariant&);

Q_SIGNALS:
    void favoriteHashChanged();
    void steamHashesChanged();

public:
    static QMap<QByteArray,Private*> gSettingsMap;

public:
    QAtomicInt iRef;
    const QByteArray iYubiKeyId;
    QDir iConfigDir;
    const QString iSettingsFile;
    QSettings* iSettings;
    QByteArray iFavoriteHash;
    QByteArrayList iSteamHashes;
};

QMap<QByteArray,YubiKeySettings::Private*> YubiKeySettings::Private::gSettingsMap;
const QString YubiKeySettings::Private::SETTINGS_FILE("settings");
const QString YubiKeySettings::Private::FAVORITE_ENTRY("Favorite");
const QString YubiKeySettings::Private::STEAM_ENTRY("Steam");
const QChar YubiKeySettings::Private::LIST_SEPARATOR(':');

YubiKeySettings::Private::Private(
    const QByteArray& aYubiKeyId) :
    iRef(1),
    iYubiKeyId(aYubiKeyId),
    iConfigDir(YubiKeyUtil::configDir(aYubiKeyId)),
    iSettingsFile(iConfigDir.filePath(SETTINGS_FILE)),
    iSettings(Q_NULLPTR)
{
    const QFileInfo settingsFile(iSettingsFile);

    // Load settings from the file (if it exists)
    if (settingsFile.isFile() && settingsFile.isReadable()) {
        HDEBUG("Loading" << qPrintable(iSettingsFile));
        iSettings = new QSettings(iSettingsFile, QSettings::IniFormat, this);
        iFavoriteHash = getFavoriteHash(iSettings);

        // Convert hex steam hashes into byte arrays
        const QStringList steamHashes(iSettings->value(STEAM_ENTRY).
            toString().split(LIST_SEPARATOR));

        iSteamHashes.reserve(steamHashes.count());
        for (QStringListIterator it(steamHashes); it.hasNext();) {
            const QByteArray hash(QByteArray::fromHex(it.next().toLatin1()));

            if (!hash.isEmpty()) {
                iSteamHashes.append(hash);
            }
        }
    }

    gSettingsMap.insert(iYubiKeyId, this);
}

YubiKeySettings::Private::~Private()
{
    gSettingsMap.remove(iYubiKeyId);
}

void
YubiKeySettings::Private::update(
    const QString& aKey,
    const QVariant& aValue)
{
    if (aValue.isValid()) {
        if (iConfigDir.exists() || iConfigDir.mkpath(".")) {
            HWARN("Writing" << qPrintable(iSettingsFile));
            if (!iSettings) {
                iSettings = new QSettings(iSettingsFile,
                    QSettings::IniFormat, this);
            }
        } else {
            HWARN("Failed to create" << qPrintable(iConfigDir.path()));
        }
        if (iSettings) {
            iSettings->setValue(aKey, aValue);
        }
    } else if (iSettings) {
        HWARN("Updating" << qPrintable(iSettingsFile));
        iSettings->remove(aKey);
    }
}

void
YubiKeySettings::Private::connect(
    YubiKeySettings* aSetting)
{
    aSetting->connect(this,
        SIGNAL(favoriteHashChanged()),
        SIGNAL(favoriteHashChanged()));
    aSetting->connect(this,
        SIGNAL(steamHashesChanged()),
        SIGNAL(steamHashesChanged()));
}

QByteArray
YubiKeySettings::Private::getFavoriteHash(
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
                return QByteArray::fromHex(favorite.toLatin1());
            } else {
                const QByteArray hash(YubiKeyUtil::nameHash(favorite));
                const QString hex(QString::fromLatin1(hash.toHex()));

                HDEBUG("Replacing favorite" << favorite << "with" << hex);
                aSettings->setValue(FAVORITE_ENTRY, hex);
                return hash;
            }
        }
    }
    return QByteArray();
}

bool
YubiKeySettings::Private::isFavoriteName(
    const QString& aName)
{
    return !aName.isEmpty() && iFavoriteHash == YubiKeyUtil::nameHash(aName);
}

void
YubiKeySettings::Private::setFavoriteHash(
    const QByteArray& aHash)
{
    if (iFavoriteHash != aHash) {
        iFavoriteHash = aHash;
        update(FAVORITE_ENTRY, iFavoriteHash.isEmpty() ? QVariant() :
            QVariant::fromValue(QString::fromLatin1(iFavoriteHash.toHex())));
        Q_EMIT favoriteHashChanged();
    }
}

void
YubiKeySettings::Private::setFavoriteName(
    const QString& aName)
{
    setFavoriteHash(aName.isEmpty() ? QByteArray() :
        YubiKeyUtil::nameHash(aName));

}

void
YubiKeySettings::Private::steamHashesUpdated()
{
    if (iSteamHashes.isEmpty()) {
        HDEBUG("No steam hashes");
        update(STEAM_ENTRY, QVariant());
    } else {
        QString value;

        for (QByteArrayListIterator it(iSteamHashes); it.hasNext();) {
            if (!value.isEmpty()) {
                value.append(LIST_SEPARATOR);
            }
            value.append(QString::fromLatin1(it.next().toHex()));
        }
        HDEBUG("Steam hashes" << value);
        update(STEAM_ENTRY, value);
    }
    Q_EMIT steamHashesChanged();
}

void
YubiKeySettings::Private::setSteamHashes(
    const QByteArrayList& aList)
{
    if (iSteamHashes != aList) {
        iSteamHashes = aList;
        steamHashesUpdated();
    }
}

void
YubiKeySettings::Private::addSteamHash(
    const QByteArray& aHash)
{
    if (!aHash.isEmpty() && !iSteamHashes.contains(aHash)) {
        iSteamHashes.append(aHash);
        qStableSort(iSteamHashes);
        steamHashesUpdated();
    }
}

void
YubiKeySettings::Private::removeSteamHash(
    const QByteArray& aHash)
{
    if (iSteamHashes.removeOne(aHash)) {
        steamHashesUpdated();
    }
}

void
YubiKeySettings::Private::tokenRenamed(
    const QString& aFrom,
    const QString& aTo)
{
    if (aFrom != aTo) {
        if (isFavoriteName(aFrom)) {
            HDEBUG("Favorite token renamed");
            setFavoriteName(aTo);
        }
        if (iSteamHashes.removeOne(YubiKeyUtil::steamNameHash(aFrom))) {
            iSteamHashes.append(YubiKeyUtil::steamNameHash(aTo));
            qStableSort(iSteamHashes);
            steamHashesUpdated();
        }
    }
}

// ==========================================================================
// YubiKeySettings
// Acts as a wrapper around a shared YubiKeySettings::Private object
// ==========================================================================

YubiKeySettings::YubiKeySettings(
    QByteArray aYubiKeyId) :
    iPrivate(Private::gSettingsMap.value(aYubiKeyId))
{
    if (iPrivate) {
        iPrivate->iRef.ref();
        iPrivate->connect(this);
    } else if (!aYubiKeyId.isEmpty()) {
        iPrivate = new Private(aYubiKeyId);
        iPrivate->connect(this);
    }
}

YubiKeySettings::YubiKeySettings(
    const YubiKeySettings& aSettings) :
    QObject(Q_NULLPTR),
    iPrivate(aSettings.iPrivate)
{
    if (iPrivate) {
        iPrivate->iRef.ref();
        iPrivate->connect(this);
    }
}

YubiKeySettings::YubiKeySettings() :
    iPrivate(Q_NULLPTR)
{}

YubiKeySettings::~YubiKeySettings()
{
    if (iPrivate) {
        iPrivate->disconnect(this);
        if (!iPrivate->iRef.deref()) {
            delete iPrivate;
        }
    }
}

YubiKeySettings&
YubiKeySettings::operator=(
    const YubiKeySettings& aSettings)
{
    if (iPrivate != aSettings.iPrivate) {
        if (iPrivate) {
            iPrivate->disconnect(this);
            if (!iPrivate->iRef.deref()) {
                delete iPrivate;
            }
        }
        iPrivate = aSettings.iPrivate;
        if (iPrivate) {
            iPrivate->iRef.ref();
            iPrivate->connect(this);
        }
    }
    return *this;
}

bool
YubiKeySettings::isValid() const
{
    return iPrivate != Q_NULLPTR;
}

QByteArray
YubiKeySettings::yubiKeyId() const
{
    return iPrivate ? iPrivate->iYubiKeyId : QByteArray();
}

QByteArray
YubiKeySettings::favoriteHash() const
{
    return iPrivate ? iPrivate->iFavoriteHash : QByteArray();
}

void
YubiKeySettings::setFavoriteHash(
    QByteArray aHash)
{
    if (iPrivate) {
        iPrivate->setFavoriteHash(aHash);
    }
}

bool
YubiKeySettings::isFavoriteName(
    QString aName) const
{
    return iPrivate && iPrivate->isFavoriteName(aName);
}

bool
YubiKeySettings::isFavoriteHash(
    QByteArray aHash) const
{
    return !aHash.isEmpty() && iPrivate && iPrivate->iFavoriteHash == aHash;
}

void
YubiKeySettings::setFavoriteName(
    QString aName)
{
    if (iPrivate) {
        iPrivate->setFavoriteName(aName);
    }
}

void
YubiKeySettings::clearFavorite()
{
    if (iPrivate) {
        iPrivate->setFavoriteHash(QByteArray());
    }
}

QByteArrayList
YubiKeySettings::steamHashes() const
{
    return iPrivate ? iPrivate->iSteamHashes : QByteArrayList();
}

bool
YubiKeySettings::isSteamHash(
    QByteArray aHash) const
{
    return iPrivate && iPrivate->iSteamHashes.contains(aHash);
}

void
YubiKeySettings::setSteamHashes(
    QByteArrayList aHashes)
{
    if (iPrivate) {
        iPrivate->setSteamHashes(aHashes);
    }
}

void
YubiKeySettings::addSteamHash(
    QByteArray aHash)
{
    if (iPrivate) {
        iPrivate->addSteamHash(aHash);
    }
}

void
YubiKeySettings::removeSteamHash(
    QByteArray aHash)
{
    if (iPrivate) {
        iPrivate->removeSteamHash(aHash);
    }
}

void
YubiKeySettings::tokenRenamed(
    QString aFrom,
    QString aTo)
{
    if (iPrivate) {
        iPrivate->tokenRenamed(aFrom, aTo);
    }
}

#include "YubiKeySettings.moc"
