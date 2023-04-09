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

#include "YubiKeyConstants.h"
#include "YubiKeyAuthListModel.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"

#include <QDir>
#include <QSettings>
#include <QCryptographicHash>

#include <ctype.h>

// Model roles
#define MODEL_ROLES_(first,role,last) \
    first(Name,name) \
    role(Type,type) \
    role(Algorithm,algorithm) \
    role(Password,password) \
    role(Favorite,favorite) \
    role(Refreshable,refreshable) \
    role(Expired,expired) \
    role(MarkedForRefresh,markedForRefresh) \
    last(MarkedForDeletion,markedForDeletion)

#define MODEL_ROLES(role) \
    MODEL_ROLES_(role,role,role)

// s(SignalName,signalName)
#define MODEL_SIGNALS(s) \
    s(YubiKeyId,yubiKeyId) \
    s(AuthList,authList) \
    s(AuthData,authData) \
    s(FavoriteTokenType,favoriteTokenType) \
    s(FavoriteMarkedForRefresh,favoriteMarkedForRefresh) \
    s(FavoriteName,favoriteName) \
    s(FavoritePassword,favoritePassword) \
    s(FavoritePasswordExpired,favoritePasswordExpired) \
    s(RefreshableTokens,refreshableTokens) \
    s(MarkedForRefresh,markedForRefresh) \
    s(MarkedForDeletion,markedForDeletion) \
    s(HaveExpiringTotpCodes,haveExpiringTotpCodes)

// ==========================================================================
// YubiKeyAuthListModel::ModelData
// ==========================================================================

class YubiKeyAuthListModel::ModelData :
    public YubiKeyConstants
{
public:
    class List;

    enum Mark {
        MarkNone,
        MarkForRefresh,
        MarkForDeletion
    };

    enum Role {
#define FIRST(X,x) FirstRole = Qt::UserRole, X##Role = FirstRole,
#define ROLE(X,x) X##Role,
#define LAST(X,x) X##Role, LastRole = X##Role
        MODEL_ROLES_(FIRST,ROLE,LAST)
#undef FIRST
#undef ROLE
#undef LAST
    };

    ModelData(const QByteArray, YubiKeyTokenType, YubiKeyAlgorithm);

    QVariant get(Role) const;

    static QString nameHashUtf8(const QByteArray&);
    static QString nameHash(const QString&);
    static YubiKeyTokenType toAuthType(uchar);
    static YubiKeyAlgorithm toAuthAlgorithm(uchar);

public:
    const QByteArray iUtf8Name;
    const QString iName;
    const QString iNameHash;
    const YubiKeyTokenType iType;
    const YubiKeyAlgorithm iAlgorithm;
    QString iPassword;
    bool iFavorite;
    bool iExpired;
    bool iRefreshable;
    Mark iMark;
};

YubiKeyAuthListModel::ModelData::ModelData(
    const QByteArray aUtf8Name,
    YubiKeyTokenType aType,
    YubiKeyAlgorithm aAlgorithm) :
    iUtf8Name(aUtf8Name),
    iName(QString::fromUtf8(aUtf8Name)),
    iNameHash(nameHashUtf8(aUtf8Name)),
    iType(aType),
    iAlgorithm(aAlgorithm),
    iFavorite(false),
    iExpired(false),
    iRefreshable(aType == YubiKeyTokenType_HOTP),
    iMark(MarkNone)
{
}

QVariant
YubiKeyAuthListModel::ModelData::get(
    Role aRole) const
{
    switch (aRole) {
    case NameRole: return iName;
    case TypeRole: return iType;
    case AlgorithmRole: return iAlgorithm;
    case PasswordRole: return iPassword;
    case FavoriteRole: return iFavorite;
    case ExpiredRole: return iExpired;
    case RefreshableRole: return iRefreshable;
    case MarkedForRefreshRole: return iMark == MarkForRefresh;
    case MarkedForDeletionRole: return iMark == MarkForDeletion;
    }
    return QVariant();
}

QString
YubiKeyAuthListModel::ModelData::nameHashUtf8(
    const QByteArray& aUtf8)
{
    return QCryptographicHash::hash(aUtf8, QCryptographicHash::Sha1).toHex();
}

QString
YubiKeyAuthListModel::ModelData::nameHash(const QString& aName)
{
    return nameHashUtf8(aName.toUtf8());
}

YubiKeyTokenType
YubiKeyAuthListModel::ModelData::toAuthType(
    uchar aTypeAlg)
{
    switch (aTypeAlg & TYPE_MASK) {
    case TYPE_HOTP: return YubiKeyTokenType_HOTP;
    case TYPE_TOTP: return YubiKeyTokenType_TOTP;
    }
    return YubiKeyTokenType_Unknown;
}

inline
YubiKeyAlgorithm
YubiKeyAuthListModel::ModelData::toAuthAlgorithm(
    uchar aTypeAlg)
{
    return YubiKeyUtil::algorithmFromValue(aTypeAlg & ALG_MASK);
}

// ==========================================================================
// YubiKeyAuthListModel::ModelData::List
// ==========================================================================

class YubiKeyAuthListModel::ModelData::List : public QList<ModelData*>
{
public:
    List() {}
    List(const QString aHexData, const List, const QStringList);

    ModelData* dataAt(int) const;
    int findUtf8(const QByteArray, int) const;
};

YubiKeyAuthListModel::ModelData::List::List(
    const QString aHexData,
    const List aOldList,
    const QStringList aRefreshList)
{
    uchar tag;
    GUtilRange resp;
    GUtilData data;

    const QByteArray bytes(YubiKeyUtil::fromHex(aHexData));
    resp.end = (resp.ptr = (guint8*)bytes.constData()) + bytes.size();

    // List Response Syntax
    //
    // Response will be a continual list of objects looking like:
    // +---------------+----------------------------------------------+
    // | Name list tag | 0x72                                         |
    // | Name length   | Length of name + 1                           |
    // | Algorithm     | High 4 bits is type, low 4 bits is algorithm |
    // | Name data     | Name                                         |
    // +---------------+----------------------------------------------+
    HDEBUG("Credentials:");
    while ((tag = YubiKeyUtil::readTLV(&resp, &data)) != 0) {
        if (tag == TLV_TAG_LIST_ENTRY && data.size >= 1) {
            const QByteArray utf8((char*)(data.bytes + 1), (int)data.size - 1);
            ModelData* newEntry = new ModelData(utf8,
                toAuthType(data.bytes[0]), toAuthAlgorithm(data.bytes[0]));
            const int oldEntryPos = aOldList.findUtf8(utf8, count());

            // Always mark HOTP codes as refreshable
            newEntry->iRefreshable =
                (newEntry->iType == YubiKeyTokenType_HOTP) ||
                aRefreshList.contains(newEntry->iName);

            // Copy other mutable attributes from the old entry
            if (oldEntryPos >= 0) {
                const ModelData* oldEntry = aOldList.at(oldEntryPos);

                newEntry->iPassword = oldEntry->iPassword;
                newEntry->iFavorite = oldEntry->iFavorite;
                newEntry->iExpired = oldEntry->iExpired;
                newEntry->iMark = oldEntry->iMark;
            }

            append(newEntry);
            HDEBUG("Entry #" << size());
            HDEBUG("  Name:" << newEntry->iName);
            HDEBUG("  Type:" << newEntry->iType);
            HDEBUG("  Alg:" << newEntry->iAlgorithm);
        }
    }
}

YubiKeyAuthListModel::ModelData*
YubiKeyAuthListModel::ModelData::List::dataAt(
    int aIndex) const
{
    return (aIndex >= 0 && aIndex < count()) ? at(aIndex) : Q_NULLPTR;
}

int
YubiKeyAuthListModel::ModelData::List::findUtf8(
    const QByteArray aUtf8,
    int aIndex) const
{
    const YubiKeyAuthListModel::ModelData* data = dataAt(aIndex);
    if (data && data->iUtf8Name == aUtf8) {
        // Most likely scenario
        return aIndex;
    } else {
        // A miss, scan the entire list
        const int n = count();
        for (int i = 0; i < n; i++) {
            if (i != aIndex && at(i)->iUtf8Name == aUtf8) {
                return i;
            }
        }
        return -1;
    }
}

// ==========================================================================
// YubiKeyAuthListModel::Private
// ==========================================================================

class YubiKeyAuthListModel::Private
{
public:
    typedef void (YubiKeyAuthListModel::*SignalEmitter)();
    typedef uint SignalMask;
    enum Signal {
#define SIGNAL_ENUM_(Name,name) Signal##Name##Changed,
        MODEL_SIGNALS(SIGNAL_ENUM_)
#undef  SIGNAL_ENUM_
        SignalCount
    };

    static const QString SETTINGS_FILE;
    static const QString FAVORITE_ENTRY;

public:
    Private(YubiKeyAuthListModel*);
    ~Private();

    void queueSignal(Signal);
    void emitQueuedSignals();
    void setYubiKeyId(const QString);
    void setItems(const ModelData::List);
    void setFavoriteTokenType(YubiKeyTokenType);
    void setFavoriteMarkedForRefresh(bool);
    void setFavoriteName(const QString);
    void setFavoritePassword(const QString);
    void setFavoritePasswordExpired(bool);
    void updateMarkedForRefresh();
    void updateMarkedForDeletion();
    void markChanged(ModelData::Mark, QVector<int>*);
    QString getFavoriteName();
    QVector<int> toggleMark(ModelData*, ModelData::Mark, bool);
    QStringList marked(ModelData::Mark aMark);

public:
    SignalMask iQueuedSignals;
    Signal iFirstQueuedSignal;
    YubiKeyAuthListModel* iModel;
    QDir iConfigDir;
    QSettings* iSettings;
    QString iYubiKeyId;
    QString iHexAuthList;
    QString iHexAuthData;
    QString iFavoriteName;
    QString iFavoritePassword;
    YubiKeyTokenType iFavoriteTokenType;
    bool iFavoriteMarkedForRefresh;
    bool iFavoritePasswordExpired;
    ModelData::List iList;
    QStringList iRefreshableTokens;
    QStringList iMarkedForRefresh;
    QStringList iMarkedForDeletion;
    bool iHaveExpiringTotpCodes;
};


const QString YubiKeyAuthListModel::Private::SETTINGS_FILE("settings");
const QString YubiKeyAuthListModel::Private::FAVORITE_ENTRY("Favorite");

YubiKeyAuthListModel::Private::Private(
    YubiKeyAuthListModel* aModel) :
    iQueuedSignals(0),
    iFirstQueuedSignal(SignalCount),
    iModel(aModel),
    iConfigDir(YubiKeyUtil::configDir()),
    iSettings(Q_NULLPTR),
    iFavoriteTokenType(YubiKeyTokenType_Unknown),
    iFavoriteMarkedForRefresh(false),
    iFavoritePasswordExpired(false),
    iHaveExpiringTotpCodes(false)
{
}

YubiKeyAuthListModel::Private::~Private()
{
    delete iSettings;
    qDeleteAll(iList);
}

void
YubiKeyAuthListModel::Private::queueSignal(
    Signal aSignal)
{
    if (aSignal >= 0 && aSignal < SignalCount) {
        const SignalMask signalBit = (SignalMask(1) << aSignal);

        if (iQueuedSignals) {
            iQueuedSignals |= signalBit;
            if (iFirstQueuedSignal > aSignal) {
                iFirstQueuedSignal = aSignal;
            }
        } else {
            iQueuedSignals = signalBit;
            iFirstQueuedSignal = aSignal;
        }
    }
}

void
YubiKeyAuthListModel::Private::emitQueuedSignals()
{
    static const SignalEmitter emitSignal [] = {
#define SIGNAL_EMITTER_(Name,name) &YubiKeyAuthListModel::name##Changed,
        MODEL_SIGNALS(SIGNAL_EMITTER_)
#undef  SIGNAL_EMITTER_
    };
    Q_STATIC_ASSERT(G_N_ELEMENTS(emitSignal) == SignalCount);
    if (iQueuedSignals) {
        // Reset first queued signal before emitting the signals.
        // Signal handlers may emit more signals.
        uint i = iFirstQueuedSignal;
        iFirstQueuedSignal = SignalCount;
        for (; i < SignalCount && iQueuedSignals; i++) {
            const SignalMask signalBit = (SignalMask(1) << i);
            if (iQueuedSignals & signalBit) {
                iQueuedSignals &= ~signalBit;
                Q_EMIT (iModel->*(emitSignal[i]))();
            }
        }
    }
}

QString
YubiKeyAuthListModel::Private::getFavoriteName()
{
    if (iSettings) {
        // Favorite name was originall stored in plain text
        static const int Sha1Len = 20 * 2; // 2 hex digits per byte
        bool isSha1 = false;
        QString favorite(iSettings->value(FAVORITE_ENTRY).toString());
        const int len = favorite.length();
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
            QString hash(ModelData::nameHash(favorite));

            HDEBUG("Replacing favorite name" << favorite << "with" << hash);
            iSettings->setValue(FAVORITE_ENTRY, hash);
            return hash;
        }
    }
    return QString();
}

void
YubiKeyAuthListModel::Private::setYubiKeyId(
    const QString aYubiKeyId)
{
    if (iYubiKeyId != aYubiKeyId) {
        iYubiKeyId = aYubiKeyId;
        queueSignal(SignalYubiKeyIdChanged);
        delete iSettings;
        if (iYubiKeyId.isEmpty()) {
            iSettings = Q_NULLPTR;
        } else {
            const QString settingsFile(iConfigDir.absoluteFilePath(iYubiKeyId) +
                QDir::separator() + SETTINGS_FILE);

            HDEBUG("Settings" << qPrintable(settingsFile));
            iConfigDir.mkpath(".");
            iSettings = new QSettings(settingsFile, QSettings::IniFormat);

            const QString favoriteName(getFavoriteName());
            const QVector<int> roles(1, ModelData::FavoriteRole);
            QString realFavoriteName, favoritePassword;
            YubiKeyTokenType favoriteTokenType = YubiKeyTokenType_Unknown;
            bool favoriteExpired = false, favoriteMfR = false;

            // Update the roles to match the settings
            for (int i = 0; i < iList.count(); i++) {
                ModelData* entry = iList.at(i);

                if (entry->iNameHash == favoriteName) {
                    realFavoriteName = favoriteName;
                    favoritePassword = entry->iPassword;
                    favoriteTokenType = entry->iType;
                    favoriteExpired = entry->iExpired;
                    favoriteMfR = (entry->iMark == ModelData::MarkForRefresh);
                    if (!entry->iFavorite) {
                        const QModelIndex idx(iModel->index(i));

                        entry->iFavorite = true;
                        Q_EMIT iModel->dataChanged(idx, idx, roles);
                    }
                } else if (entry->iFavorite) {
                    const QModelIndex idx(iModel->index(i));

                    entry->iFavorite = false;
                    Q_EMIT iModel->dataChanged(idx, idx, roles);
                }
            }

            setFavoriteName(realFavoriteName);
            setFavoriteTokenType(favoriteTokenType);
            setFavoritePassword(favoritePassword);
            setFavoritePasswordExpired(favoriteExpired);
            setFavoriteMarkedForRefresh(favoriteMfR);
        }
    }
}

void
YubiKeyAuthListModel::Private::setItems(
    const ModelData::List aList)
{
    const bool hadExpiringTotpCodes = iHaveExpiringTotpCodes;
    const QStringList prevMarkedForRefresh(iMarkedForRefresh);
    const QStringList prevMarkedForDeletion(iMarkedForDeletion);
    const int n = aList.count();

    qDeleteAll(iList);

    iList = aList;
    iHaveExpiringTotpCodes = false;
    iMarkedForRefresh.clear();
    iMarkedForDeletion.clear();

    bool favoriteExpired = false, favoriteMfR = false;
    YubiKeyTokenType favoriteTokenType = YubiKeyTokenType_Unknown;
    QString realFavoriteName, favoritePassword;
    const QString favoriteName(getFavoriteName());

    for (int i = 0; i < n; i++) {
        ModelData* entry = iList.at(i);

        entry->iFavorite = (entry->iNameHash == favoriteName);
        if (entry->iFavorite) {
            realFavoriteName = entry->iName;
            favoritePassword = entry->iPassword;
            favoriteTokenType = entry->iType;
            favoriteExpired = entry->iExpired;
            favoriteMfR = (entry->iMark == ModelData::MarkForRefresh);
        }
        if (prevMarkedForRefresh.contains(entry->iName)) {
            iMarkedForRefresh.append(entry->iName);
            entry->iMark = ModelData::MarkForRefresh;
        } else if (prevMarkedForDeletion.contains(entry->iName)) {
            iMarkedForDeletion.append(entry->iName);
            entry->iMark = ModelData::MarkForDeletion;
        } else {
            entry->iMark = ModelData::MarkNone;
        }
        if (!entry->iRefreshable && !entry->iExpired) {
            iHaveExpiringTotpCodes = true;
        }
    }

    setFavoriteName(realFavoriteName);
    setFavoritePassword(favoritePassword);
    setFavoritePasswordExpired(favoriteExpired);
    setFavoriteTokenType(favoriteTokenType);
    setFavoriteMarkedForRefresh(favoriteMfR);
    if (prevMarkedForRefresh != iMarkedForRefresh) {
        queueSignal(SignalMarkedForRefreshChanged);
    }
    if (prevMarkedForDeletion != iMarkedForDeletion) {
        queueSignal(SignalMarkedForDeletionChanged);
    }
    if (hadExpiringTotpCodes != iHaveExpiringTotpCodes) {
        queueSignal(SignalHaveExpiringTotpCodesChanged);
    }
}

void
YubiKeyAuthListModel::Private::setFavoriteTokenType(
    YubiKeyTokenType aTokenType)
{
    if (iFavoriteTokenType != aTokenType) {
        iFavoriteTokenType = aTokenType;
        queueSignal(SignalFavoriteTokenTypeChanged);
    }
}

void
YubiKeyAuthListModel::Private::setFavoriteMarkedForRefresh(
    bool aMarkedForRefresh)
{
    if (iFavoriteMarkedForRefresh != aMarkedForRefresh) {
        iFavoriteMarkedForRefresh = aMarkedForRefresh;
        queueSignal(SignalFavoriteMarkedForRefreshChanged);
    }
}

void
YubiKeyAuthListModel::Private::setFavoriteName(
    const QString aName)
{
    if (iFavoriteName != aName) {
        iFavoriteName = aName;
        queueSignal(SignalFavoriteNameChanged);
    }
}

void
YubiKeyAuthListModel::Private::setFavoritePassword(
    const QString aPassword)
{
    if (iFavoritePassword != aPassword) {
        iFavoritePassword = aPassword;
        queueSignal(SignalFavoritePasswordChanged);
    }
}

void
YubiKeyAuthListModel::Private::setFavoritePasswordExpired(
    bool aExpired)
{
    if (iFavoritePasswordExpired != aExpired) {
        iFavoritePasswordExpired = aExpired;
        queueSignal(SignalFavoritePasswordExpiredChanged);
    }
}

void
YubiKeyAuthListModel::Private::updateMarkedForRefresh()
{
    const QStringList prevMarkedForRefresh(iMarkedForRefresh);

    iMarkedForRefresh = marked(ModelData::MarkForRefresh);
    if (prevMarkedForRefresh != iMarkedForRefresh) {
        HDEBUG(iMarkedForRefresh);
        queueSignal(SignalMarkedForRefreshChanged);
    }
    setFavoriteMarkedForRefresh(!iFavoriteName.isEmpty() &&
        iMarkedForRefresh.contains(iFavoriteName));
}

void
YubiKeyAuthListModel::Private::updateMarkedForDeletion()
{
    const QStringList prevMarkedForDeletion(iMarkedForDeletion);

    iMarkedForDeletion = marked(ModelData::MarkForDeletion);
    if (prevMarkedForDeletion != iMarkedForDeletion) {
        HDEBUG(iMarkedForDeletion);
        queueSignal(SignalMarkedForDeletionChanged);
    }
}

QStringList
YubiKeyAuthListModel::Private::marked(
    ModelData::Mark aMark)
{
    const int n = iList.count();
    QStringList list;

    for (int i = 0; i < n; i++) {
        const ModelData* entry = iList.at(i);

        if (entry->iMark == aMark) {
            list.append(entry->iName);
        }
    }
    return list;
}

void
YubiKeyAuthListModel::Private::markChanged(
    ModelData::Mark aMark,
    QVector<int>* aRoles)
{
    switch (aMark) {
    case ModelData::MarkForRefresh:
        aRoles->append(ModelData::MarkedForRefreshRole);
        updateMarkedForRefresh();
        break;
    case ModelData::MarkForDeletion:
        aRoles->append(ModelData::MarkedForDeletionRole);
        updateMarkedForDeletion();
        break;
    case ModelData::MarkNone:
        break;
    }
}

QVector<int>
YubiKeyAuthListModel::Private::toggleMark(
    ModelData* aData,
    ModelData::Mark aMark,
    bool aTurnOn)
{
    ModelData::Mark prevMark = aData->iMark;
    QVector<int> roles;

    if (aTurnOn) {
        aData->iMark = aMark;
    } else if (aData->iMark == aMark) {
        aData->iMark = ModelData::MarkNone;
    }

    if (aData->iMark != prevMark) {
        markChanged(prevMark, &roles);
        markChanged(aData->iMark, &roles);
    }
    return roles;
}

// ==========================================================================
// YubiKeyAuthListModel
// ==========================================================================

YubiKeyAuthListModel::YubiKeyAuthListModel(
    QObject* aParent) :
    QAbstractListModel(aParent),
    iPrivate(new Private(this))
{
}

YubiKeyAuthListModel::~YubiKeyAuthListModel()
{
    delete iPrivate;
}

void
YubiKeyAuthListModel::setYubiKeyId(
    const QString aYubiKeyId)
{
    iPrivate->setYubiKeyId(aYubiKeyId);
    iPrivate->emitQueuedSignals();
}

QString
YubiKeyAuthListModel::yubiKeyId() const
{
    return iPrivate->iYubiKeyId;
}

QString
YubiKeyAuthListModel::authList() const
{
    return iPrivate->iHexAuthList;
}

void
YubiKeyAuthListModel::setAuthList(
    const QString aHexAuthList)
{
    if (iPrivate->iHexAuthList != aHexAuthList) {
        // All this just to avoid resetting the entire model
        // which resets view position too.
        const ModelData::List newList(aHexAuthList, iPrivate->iList,
            iPrivate->iRefreshableTokens);
        const int prevCount = iPrivate->iList.count();
        const int newCount = newList.count();

        HDEBUG(aHexAuthList);
        iPrivate->iHexAuthList = aHexAuthList;
        iPrivate->queueSignal(Private::SignalAuthListChanged);
        if (newCount < prevCount) {
            beginRemoveRows(QModelIndex(), newCount, prevCount - 1);
            iPrivate->setItems(newList);
            endRemoveRows();
            if (newCount > 0) {
                Q_EMIT dataChanged(index(0), index(newCount - 1));
            }
        } else if (newCount > prevCount) {
            beginInsertRows(QModelIndex(), prevCount, newCount - 1);
            iPrivate->setItems(newList);
            endInsertRows();
            if (prevCount > 0) {
                Q_EMIT dataChanged(index(0), index(prevCount - 1));
            }
        } else {
            iPrivate->setItems(newList);
            if (newCount > 0) {
                Q_EMIT dataChanged(index(0), index(newCount - 1));
            }
        }
        iPrivate->emitQueuedSignals();
    }
}

QString
YubiKeyAuthListModel::authData() const
{
    return iPrivate->iHexAuthData;
}

void
YubiKeyAuthListModel::setAuthData(
    const QString aHexAuthData)
{
    if (iPrivate->iHexAuthData != aHexAuthData) {
        iPrivate->iHexAuthData = aHexAuthData;
        iPrivate->queueSignal(Private::SignalAuthDataChanged);
        HDEBUG(aHexAuthData);

        uchar tag;
        GUtilRange resp;
        GUtilData data;
        bool refreshMarksUpdated = false;

        const QByteArray bytes(YubiKeyUtil::fromHex(aHexAuthData));
        resp.end = (resp.ptr = (guint8*)bytes.constData()) + bytes.size();

        // Calculate All Response Syntax
        //
        // For HOTP the response tag is 0x77 (No response). For credentials
        // requiring touch the response tag is 0x7c (No response).
        // The response will be a list of the following objects:
        // +---------------+----------------------------------------------+
        // | Name tag      | 0x71                                         |
        // | Name length   | Length of name                               |
        // | Name data     | Name                                         |
        // | Response tag  | 0x77 for HOTP, 0x7c for touch, 0x75 for full |
        // |               | response or 0x76 for truncated response      |
        // +---------------+----------------------------------------------+
        // | Response len  | Length of response + 1                       |
        // | Digits        | Number of digits in the OATH code            |
        // | Response data | Response                                     |
        // +---------------+----------------------------------------------+
        int currentPos = -1, count = 0;
        QVector<int> roles;

        while ((tag = YubiKeyUtil::readTLV(&resp, &data)) != 0) {
            switch (tag) {
            case YubiKeyConstants::TLV_TAG_NAME:
                {
                    const QByteArray utf8((char*)data.bytes, (int)data.size);
                    currentPos = iPrivate->iList.findUtf8(utf8, count++);
                    if (currentPos < 0) {
                        HDEBUG("Entry" << QString::fromUtf8(utf8) << "not found");
                    }
                }
                break;
            case YubiKeyConstants::TLV_TAG_RESPONSE_FULL:
                // Require at least 20 bytes of response data
                if (currentPos >= 0 && data.size > 20) {
                    ModelData* entry = iPrivate->iList.at(currentPos);

                    // First byte: Number of digits in the OATH code
                    const uint digits = data.bytes[0];
                    uint maxPass = 10;
                    for (uint i = 1; i < digits; i++) {
                        maxPass *= 10;
                    }

                    // The rest is the calculated hash
                    data.bytes++;
                    data.size--;
                    const uint off = data.bytes[data.size - 1] & 0x0f;
                    const uint miniHash = be32toh(*(guint32*)(data.bytes + off)) & 0x7fffffff;
                    const uint pass = miniHash % maxPass;
                    const QString strPass(QString().sprintf("%0*u", digits, pass));

                    roles.resize(0);
                    // Refresh is considered done even if the code didn't change
                    if (entry->iMark == ModelData::MarkForRefresh) {
                        entry->iMark = ModelData::MarkNone;
                        roles.append(ModelData::MarkedForRefreshRole);
                        refreshMarksUpdated = true;
                    }
                    if (entry->iPassword != strPass) {
                        entry->iPassword = strPass;
                        roles.append(ModelData::PasswordRole);
                        HDEBUG((currentPos + 1) << strPass);
                        if (entry->iFavorite) {
                            iPrivate->setFavoritePassword(strPass);
                            iPrivate->setFavoritePasswordExpired(false);
                        }
                        if (entry->iExpired) {
                            entry->iExpired = false;
                            roles.append(ModelData::ExpiredRole);
                        }
                        if (!iPrivate->iHaveExpiringTotpCodes &&
                            entry->iType == YubiKeyTokenType_TOTP) {
                            iPrivate->iHaveExpiringTotpCodes = true;
                            iPrivate->queueSignal(Private::SignalHaveExpiringTotpCodesChanged);
                        }
                    }
                    if (!roles.isEmpty()) {
                        const QModelIndex idx(index(currentPos));
                        Q_EMIT dataChanged(idx, idx, roles);
                    }
                    break;
                }
                /* fallthrough */
            default:
                HDEBUG("Ignoring tag" << hex << tag);
            }
        }

        if (refreshMarksUpdated) {
            iPrivate->updateMarkedForRefresh();
        }
        iPrivate->emitQueuedSignals();
    }
}

QStringList
YubiKeyAuthListModel::refreshableTokens() const
{
    return iPrivate->iRefreshableTokens;
}

void
YubiKeyAuthListModel::setRefreshableTokens(
    const QStringList aRefreshableTokens)
{
    if (iPrivate->iRefreshableTokens != aRefreshableTokens) {
        iPrivate->iRefreshableTokens = aRefreshableTokens;
        iPrivate->queueSignal(Private::SignalRefreshableTokensChanged);

        const QVector<int> roles(1, ModelData::RefreshableRole);

        HDEBUG(aRefreshableTokens);
        for (int i = 0; i < iPrivate->iList.count(); i++) {
            // Always mark HOTP codes as refreshable
            ModelData* entry = iPrivate->iList.at(i);
            const bool refreshable = (entry->iType == YubiKeyTokenType_HOTP) ||
                iPrivate->iRefreshableTokens.contains(entry->iName);

            if (entry->iRefreshable != refreshable) {
                const QModelIndex idx(index(i));

                HDEBUG(entry->iName << (refreshable ? "" : "not") << "refreshable");
                entry->iRefreshable = refreshable;
                Q_EMIT dataChanged(idx, idx, roles);
            }
        }
        iPrivate->emitQueuedSignals();
    }
}

YubiKeyTokenType
YubiKeyAuthListModel::favoriteTokenType() const
{
    return iPrivate->iFavoriteTokenType;
}

bool
YubiKeyAuthListModel::favoriteMarkedForRefresh() const
{
    return iPrivate->iFavoriteMarkedForRefresh;
}

QString
YubiKeyAuthListModel::favoriteName() const
{
    return iPrivate->iFavoriteName;
}

QString
YubiKeyAuthListModel::favoritePassword() const
{
    return iPrivate->iFavoritePassword;
}

bool
YubiKeyAuthListModel::favoritePasswordExpired() const
{
    return iPrivate->iFavoritePasswordExpired;
}

QStringList
YubiKeyAuthListModel::markedForRefresh() const
{
    return iPrivate->iMarkedForRefresh;
}

QStringList
YubiKeyAuthListModel::markedForDeletion() const
{
    return iPrivate->iMarkedForDeletion;
}

bool
YubiKeyAuthListModel::haveExpiringTotpCodes() const
{
    return iPrivate->iHaveExpiringTotpCodes;
}

void
YubiKeyAuthListModel::totpCodesExpired()
{
    if (iPrivate->iHaveExpiringTotpCodes) {
        iPrivate->iHaveExpiringTotpCodes = false;
        iPrivate->queueSignal(Private::SignalHaveExpiringTotpCodesChanged);

        const QVector<int> roles(1, ModelData::ExpiredRole);

        for (int i = 0; i < iPrivate->iList.count(); i++) {
            ModelData* entry = iPrivate->iList.at(i);

            // Only TOTP codes expire
            if (entry->iType == YubiKeyTokenType_TOTP && !entry->iExpired) {
                const QModelIndex idx(index(i));

                HDEBUG(entry->iName << "expired");
                entry->iExpired = true;
                Q_EMIT dataChanged(idx, idx, roles);
            }
        }
        iPrivate->emitQueuedSignals();
    }
}

Qt::ItemFlags
YubiKeyAuthListModel::flags(
    const QModelIndex& aIndex) const
{
    return QAbstractListModel::flags(aIndex) | Qt::ItemIsEditable;
}

QHash<int,QByteArray>
YubiKeyAuthListModel::roleNames() const
{
    QHash<int,QByteArray> roles;
#define ROLE(X,x) roles.insert(ModelData::X##Role, #x);
MODEL_ROLES(ROLE)
#undef ROLE
    return roles;
}

int
YubiKeyAuthListModel::rowCount(
    const QModelIndex& aParent) const
{
    return iPrivate->iList.count();
}

QVariant
YubiKeyAuthListModel::data(
    const QModelIndex& aIndex,
    int aRole) const
{
    const ModelData* entry = iPrivate->iList.dataAt(aIndex.row());
    return entry ? entry->get((ModelData::Role)aRole) : QVariant();
}

bool
YubiKeyAuthListModel::setData(
    const QModelIndex& aIndex,
    const QVariant& aValue,
    int aRole)
{
    ModelData* data = iPrivate->iList.dataAt(aIndex.row());
    bool ok = false;

    if (data) {
        QVector<int> roles;
        bool b;

        switch ((ModelData::Role)aRole) {
        case ModelData::FavoriteRole:
            b = aValue.toBool();
            HDEBUG(aIndex.row() << "favorite" << b);
            if (data->iFavorite != b) {
                QSettings* settings = iPrivate->iSettings;

                roles.append(aRole);
                data->iFavorite = b;

                if (settings) {
                    if (b) {
                        settings->setValue(Private::FAVORITE_ENTRY, data->iNameHash);
                    } else {
                        settings->remove(Private::FAVORITE_ENTRY);
                    }
                }

                if (b) {
                    // There is only one favorite
                    iPrivate->setFavoriteName(data->iName);
                    iPrivate->setFavoritePassword(data->iPassword);
                    iPrivate->setFavoriteTokenType(data->iType);
                    iPrivate->setFavoriteMarkedForRefresh(data->iMark == ModelData::MarkForRefresh);
                    for (int i = 0; i < iPrivate->iList.count(); i++) {
                        ModelData* other = iPrivate->iList.at(i);

                        if (other != data && other->iFavorite) {
                            other->iFavorite = false;
                            const QModelIndex idx(index(i));
                            Q_EMIT dataChanged(idx, idx, roles);
                            break;
                        }
                    }
                } else {
                    iPrivate->setFavoriteName(QString());
                    iPrivate->setFavoritePassword(QString());
                    iPrivate->setFavoriteTokenType(YubiKeyTokenType_Unknown);
                    iPrivate->setFavoriteMarkedForRefresh(false);
                }
                iPrivate->emitQueuedSignals();
            }
            ok = true;
            break;
        case ModelData::MarkedForRefreshRole:
            b = aValue.toBool();
            HDEBUG(aIndex.row() << "markedForRefresh" << b);
            roles = iPrivate->toggleMark(data, ModelData::MarkForRefresh, b);
            ok = true;
            break;
        case ModelData::MarkedForDeletionRole:
            b = aValue.toBool();
            HDEBUG(aIndex.row() << "markedForDeletion" << b);
            roles = iPrivate->toggleMark(data, ModelData::MarkForDeletion, b);
            ok = true;
            break;
        case ModelData::NameRole:
        case ModelData::TypeRole:
        case ModelData::AlgorithmRole:
        case ModelData::PasswordRole:
        case ModelData::ExpiredRole:
        case ModelData::RefreshableRole:
            HDEBUG(aIndex.row() << aRole << "nope");
            break;
        }

        if (!roles.isEmpty()) {
            Q_EMIT dataChanged(aIndex, aIndex, roles);
            iPrivate->emitQueuedSignals();
        }
    }
    return ok;
}
