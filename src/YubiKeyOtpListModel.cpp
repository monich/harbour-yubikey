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

#include "YubiKeyOtpListModel.h"

#include "YubiKey.h"
#include "YubiKeySettings.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"
#include "HarbourParentSignalQueueObject.h"
#include "HarbourUtil.h"

#include <QtCore/QHash>
#include <QtCore/QListIterator>
#include <QtCore/QPointer>
#include <QtCore/QSet>

// Model roles
#define MODEL_ROLES_(first,role,last) \
    first(Name,name) \
    role(NewName,newName) \
    role(Type,type) \
    role(Algorithm,algorithm) \
    role(Password,password) \
    role(Steam,steam) \
    role(Favorite,favorite) \
    role(EntryOp,entryOp) \
    last(EntryOpState,entryOpState)

#define MODEL_ROLES(role) \
    MODEL_ROLES_(role,role,role)

// s(SignalName,signalName)
#define QUEUED_SIGNALS(s) \
    s(YubiKey,yubiKey) \
    s(FavoriteTokenType,favoriteTokenType) \
    s(FavoriteMarkedForRefresh,favoriteMarkedForRefresh) \
    s(FavoriteName,favoriteName) \
    s(FavoritePassword,favoritePassword)

#if HARBOUR_DEBUG
QDebug
operator<<(
    QDebug aDebug,
    YubiKeyOtpListModel::EntryOp aEntryOp)
{
    #define ENTRY_OPS(o) o(None) o(Refresh) o(Rename) o(Delete)
    switch (aEntryOp) {
        #define OP_(o) case YubiKeyOtpListModel::EntryOp##o: \
        return (aDebug << "Op" #o);
        ENTRY_OPS(OP_)
        #undef OP_
    }
    return aDebug << (int)aEntryOp;
}

QDebug
operator<<(
    QDebug aDebug,
    YubiKeyOtpListModel::EntryOpState aState)
{
    #define ENTRY_OP_STATES(s) s(None) s(Queued) s(Active) s(Finished) s(Failed)
    switch (aState) {
        #define OP_STATE_(s) case YubiKeyOtpListModel::EntryOpState##s: \
        return (aDebug << "OpState" #s);
        ENTRY_OP_STATES(OP_STATE_)
        #undef OP_STATE_
    }
    return aDebug << (int)aState;
}
#endif // HARBOUR_DEBUG

// ==========================================================================
// YubiKeyOtpListModel::Entry
// ==========================================================================

class YubiKeyOtpListModel::Entry
{
public:
    enum Role {
        #define FIRST(X,x) FirstRole = Qt::UserRole, X##Role = FirstRole,
        #define ROLE(X,x) X##Role,
        #define LAST(X,x) X##Role, LastRole = X##Role
        MODEL_ROLES_(FIRST,ROLE,LAST)
        #undef FIRST
        #undef ROLE
        #undef LAST
    };

    // QtCreator syntax highlighter gets confused by the above macro magic.
    // Somehow this stupid enum unconfuses it :/
    enum { _ };

    Entry(const YubiKeyOtp&);

    QVariant get(Role) const;
    bool canBeSteamToken() const;
    bool updatePassword();
    EntryOpState currentOpState() const;
    QSet<int> setOp(YubiKeyOp*, EntryOp, const QSet<int>& aRoles = QSet<int>());
    QSet<int> updateOp();

public:
    YubiKeyOtp iOtp;
    QString iName;
    QString iNewName;
    QByteArray iNameHash;
    QByteArray iSteamHash;
    QString iPassword;
    bool iSteam;
    bool iFavorite;
    QPointer<YubiKeyOp> iOp;
    EntryOp iOpType;
    EntryOpState iOpState;
};

YubiKeyOtpListModel::Entry::Entry(
    const YubiKeyOtp& aOtp) :
    iOtp(aOtp),
    iName(QString::fromUtf8(aOtp.iName)),
    iNameHash(YubiKeyUtil::hashUtf8(aOtp.iName)),
    iSteamHash(YubiKeyUtil::steamHashUtf8(aOtp.iName)),
    iSteam(false),
    iFavorite(false),
    iOpType(EntryOpNone),
    iOpState(EntryOpStateNone)
{}

QVariant
YubiKeyOtpListModel::Entry::get(
    Role aRole) const
{
    switch (aRole) {
    case NameRole: return iName;
    case NewNameRole: return iNewName;
    case TypeRole: return iOtp.iType;
    case AlgorithmRole: return iOtp.iAlg;
    case PasswordRole: return iPassword;
    case SteamRole: return iSteam;
    case FavoriteRole: return iFavorite;
    case EntryOpRole: return iOpType;
    case EntryOpStateRole: return iOpState;
    }
    return QVariant();
}

bool
YubiKeyOtpListModel::Entry::canBeSteamToken() const
{
    return iOtp.iType == YubiKeyTokenType_TOTP;
}

bool
YubiKeyOtpListModel::Entry::updatePassword()
{
    QString strPass;

    if (iOtp.iMiniHash && iOtp.iDigits) {
        if (iSteam) {
            static const QString ALPHABET("23456789BCDFGHJKMNPQRTVWXY");
            uint pass = iOtp.iMiniHash;

            // Steam codes always have 5 symbols, ignore the digit count.
            // And YubiKey requires the number of digits to be at least 6,
            // PUT command with 5 digits fails with code 6A80 (Wrong syntax)
            for (uint i = 0; i < 5; i++) {
                strPass.append(ALPHABET.at(pass % ALPHABET.size()));
                pass /= ALPHABET.size();
            }
        } else {
            uint maxPass = 10;

            for (uint i = 1; i < iOtp.iDigits; i++) {
                maxPass *= 10;
            }
            const uint pass = iOtp.iMiniHash % maxPass;
            strPass = QString().sprintf("%0*u", iOtp.iDigits, pass);
        }
    }

    if (iPassword != strPass) {
        iPassword = strPass;
        return true;
    } else {
        return false;
    }
}

YubiKeyOtpListModel::EntryOpState
YubiKeyOtpListModel::Entry::currentOpState() const
{
    if (iOp) {
        switch (iOp->opState()) {
        case YubiKeyOp::OpQueued:
            return EntryOpStateQueued;
        case YubiKeyOp::OpActive:
            return EntryOpStateActive;
        case YubiKeyOp::OpCancelled:
            return EntryOpStateNone;
        case YubiKeyOp::OpFinished:
            return EntryOpStateFinished;
        case YubiKeyOp::OpFailed:
            return EntryOpStateFailed;
        }
    }
    return EntryOpStateNone;
}

QSet<int>
YubiKeyOtpListModel::Entry::setOp(
    YubiKeyOp* aOp,
    EntryOp aOpType,
    const QSet<int>& aRoles)
{
    iOp = aOp;

    QSet<int> roles(aRoles);
    const EntryOp opType = iOp ? aOpType : EntryOpNone;
    const EntryOpState opState = currentOpState();

    if (iOpType != opType) {
        HDEBUG(iOtp.iName.constData() << iOpType << "=>" << opType);
        iOpType = opType;
        roles.insert(EntryOpRole);
    }

    if (iOpState != opState) {
        HDEBUG(iOtp.iName.constData() << iOpState << "=>" << opState);
        iOpState = opState;
        roles.insert(EntryOpStateRole);
    }

    return roles;
}

QSet<int>
YubiKeyOtpListModel::Entry::updateOp()
{
    EntryOpState opState = iOpState;
    QSet<int> roles;

    if (iOp) {
        opState = currentOpState();
    } else {
        switch (iOpState) {
        case EntryOpStateQueued:
        case EntryOpStateActive:
            // Can't be queue or active without the op
            opState = EntryOpStateNone;
            break;
        case EntryOpStateNone:
        case EntryOpStateFinished:
        case EntryOpStateFailed:
            break;
        }
    }

    if (iOpState != opState) {
        HDEBUG(iOtp.iName.constData() << iOpState << "=>" << opState);
        iOpState = opState;
        roles.insert(EntryOpStateRole);
    }

    return roles;
}

// ==========================================================================
// YubiKeyOtpListModel::Private
// ==========================================================================

enum YubiKeyOtpListModelSignal {
    #define SIGNAL_ENUM_(Name,name) Signal##Name##Changed,
    QUEUED_SIGNALS(SIGNAL_ENUM_)
    #undef  SIGNAL_ENUM_
    YubiKeyOtpListModelSignalCount
};

typedef HarbourParentSignalQueueObject<YubiKeyOtpListModel,
    YubiKeyOtpListModelSignal, YubiKeyOtpListModelSignalCount>
    YubiKeyOtpListModelPrivateBase;

class YubiKeyOtpListModel::Private :
    public YubiKeyOtpListModelPrivateBase
{
    Q_OBJECT
    static const SignalEmitter gSignalEmitters[];

public:
    Private(YubiKeyOtpListModel*);

    YubiKeyOtpListModel* parentModel();
    void setYubiKey(YubiKey*);
    void setYubiKeyId(QByteArray);
    void setOtpList(const QList<YubiKeyOtp>&);
    void setUpdatingPasswords(bool);
    void setFavoriteTokenType(YubiKeyTokenType);
    void setFavoriteMarkedForRefresh(bool);
    void setFavoriteName(const QString&);
    void setFavoritePassword(const QString&);
    void setFavorite(const Entry&);
    void clearFavorite();
    bool updateFavorite(Entry&);
    bool updateFavoriteMarkedForRefresh(Entry&);
    static int findName(const QList<Entry>&, const QByteArray&);
    int findName(const QByteArray&) const;
    int findOp(YubiKeyOp*) const;
    void startOp(int, YubiKeyOp*, EntryOp, const QSet<int>& aRoles = QSet<int>());
    void deleteToken(QByteArray);
    void refreshToken(QByteArray);
    void renameToken(QByteArray, const QString&);
    void cancelOp(QByteArray);
    void signalEntryChanges(int, const QSet<int>&);

public Q_SLOTS:
    void onYubiKeyIdChanged();
    void onYubiKeyOtpListChanged();
    void onYubiKeyDestroyed(QObject*);
    void onYubiKeyUpdatingPasswordsChanged();
    void onEntryOpStateChanged();

public:
    QPointer<YubiKey> iYubiKey;
    YubiKeySettings iYubiKeySettings;
    QList<Entry> iList;
    YubiKeyTokenType iFavoriteTokenType;
    bool iFavoriteMarkedForRefresh;
    QString iFavoriteName;
    QString iFavoritePassword;
};

/* static */
const YubiKeyOtpListModelPrivateBase::SignalEmitter
YubiKeyOtpListModel::Private::gSignalEmitters [] = {
    #define SIGNAL_EMITTER_(Name,name) &YubiKeyOtpListModel::name##Changed,
    QUEUED_SIGNALS(SIGNAL_EMITTER_)
    #undef  SIGNAL_EMITTER_
};

YubiKeyOtpListModel::Private::Private(
    YubiKeyOtpListModel* aParent) :
    YubiKeyOtpListModelPrivateBase(aParent, gSignalEmitters),
    iFavoriteTokenType(YubiKeyTokenType_Unknown),
    iFavoriteMarkedForRefresh(false)
{}

YubiKeyOtpListModel*
YubiKeyOtpListModel::Private::parentModel()
{
    return qobject_cast<YubiKeyOtpListModel*>(parent());
}

void
YubiKeyOtpListModel::Private::onYubiKeyIdChanged()
{
    setYubiKeyId(iYubiKey->yubiKeyId());
}

void
YubiKeyOtpListModel::Private::onYubiKeyOtpListChanged()
{
    setOtpList(iYubiKey->otpList());
}

void
YubiKeyOtpListModel::Private::onYubiKeyDestroyed(
    QObject*)
{
    // This signal is emitted immediately before the object obj is destroyed,
    // but after any instances of QPointer have been notified, meaning that
    // our QPointer<YubiKey> is probably null at this point.
    if (!iYubiKey) {
        setOtpList(QList<YubiKeyOtp>());
        setYubiKeyId(QByteArray());
    }
}

void
YubiKeyOtpListModel::Private::setYubiKey(
    YubiKey* aYubiKey)
{
    if (iYubiKey != aYubiKey) {
        if (iYubiKey) {
            iYubiKey->disconnect(this);
        }
        iYubiKey = aYubiKey;
        queueSignal(SignalYubiKeyChanged);
        if (aYubiKey) {
            connect(aYubiKey, SIGNAL(yubiKeyIdChanged()),
                SLOT(onYubiKeyIdChanged()));
            connect(aYubiKey, SIGNAL(otpListChanged()),
                SLOT(onYubiKeyOtpListChanged()));
            connect(aYubiKey, SIGNAL(updatingPasswordsChanged()),
                SLOT(onYubiKeyUpdatingPasswordsChanged()));
            connect(aYubiKey, SIGNAL(destroyed(QObject*)),
                SLOT(onYubiKeyDestroyed(QObject*)));
            setYubiKeyId(aYubiKey->yubiKeyId());
            setOtpList(aYubiKey->otpList());
        } else {
            setOtpList(QList<YubiKeyOtp>());
            setYubiKeyId(QByteArray());
        }
    }
}

void
YubiKeyOtpListModel::Private::setYubiKeyId(
    QByteArray aYubiKeyId)
{
    if (iYubiKeySettings.yubiKeyId() != aYubiKeyId) {
        YubiKeyOtpListModel* model = parentModel();
        QVector<int> roles;

        HDEBUG(aYubiKeyId.toHex().constData());
        queueSignal(SignalYubiKeyChanged);
        iYubiKeySettings = YubiKeySettings(aYubiKeyId);

        // Update the roles to match the settings
        for (int i = 0; i < iList.count(); i++) {
            Entry& entry = iList[i];

            roles.resize(0);
            if (updateFavorite(entry)) {
                roles.append(Entry::FavoriteRole);
            }

            if (iYubiKeySettings.isSteamHash(entry.iSteamHash)) {
                if (!entry.iSteam) {
                    entry.iSteam = true;
                    roles.append(Entry::SteamRole);
                }
            } else if (entry.iSteam) {
                entry.iSteam = false;
                roles.append(Entry::SteamRole);
            }

            if (!roles.isEmpty()) {
                const QModelIndex idx(model->index(i));

                Q_EMIT model->dataChanged(idx, idx, roles);
            }
        }
    }
}

void
YubiKeyOtpListModel::Private::setOtpList(
    const QList<YubiKeyOtp>& aList)
{
    const QList<Entry> oldList(iList);
    const int oldCount = oldList.count();
    const int newCount = aList.count();
    const int minCount = qMin(oldCount, newCount);
    YubiKeyOtpListModel* model = parentModel();

    if (newCount > oldCount) {
        model->beginInsertRows(QModelIndex(), oldCount, newCount - 1);
        for (int i = oldCount; i < newCount; i++) {
            Entry entry(aList.at(i));
            const int k = findName(oldList, entry.iOtp.iName);

            if (k >= 0) {
                const Entry& oldEntry = oldList.at(k);

                if (oldEntry.iOp && !oldEntry.iOp->opIsDone()) {
                    entry.iOp = oldEntry.iOp;
                    entry.iOpType = oldEntry.iOpType;
                    entry.iOpState = oldEntry.iOpState;
                }
            }
            entry.iSteam = iYubiKeySettings.isSteamHash(entry.iSteamHash);
            entry.updatePassword();
            updateFavorite(entry);
            iList.append(entry);
            HDEBUG("+" << entry.iOtp);
        }
        model->endInsertRows();
    } else if (newCount < oldCount) {
        model->beginRemoveRows(QModelIndex(), newCount, oldCount - 1);
        while (iList.count() > newCount) {
            const Entry removed(iList.takeLast());

            HDEBUG("-" << removed.iOtp);
            if (removed.iFavorite) {
                clearFavorite();
                // Leaving favorite hash in the settings because we don't
                // want to lose it when the list is being reset. We remove it
                // only when the item is explicitly removed from the cover.
            }
        }
        model->endRemoveRows();
    }

    // Update the intersection
    QSet<int> roles;

    for (int i = 0; i < minCount; i++) {
        const YubiKeyOtp otp(aList.at(i));
        Entry& entry = iList[i];
        EntryOp opType = EntryOpNone;
        EntryOpState opState = EntryOpStateNone;
        const int k = findName(oldList, entry.iOtp.iName);

        if (entry.iOtp != otp) {
            if (entry.iOtp.iName != otp.iName) {
                entry.iOtp.iName = otp.iName;
                entry.iName = QString::fromUtf8(otp.iName);
                entry.iNameHash = YubiKeyUtil::hashUtf8(otp.iName);
                entry.iSteamHash = YubiKeyUtil::steamHashUtf8(otp.iName);
                roles.insert(Entry::NameRole);
            }

            if (entry.iOtp.iType != otp.iType) {
                entry.iOtp.iType = otp.iType;
                roles.insert(Entry::TypeRole);
            }

            if (entry.iOtp.iAlg != otp.iAlg) {
                entry.iOtp.iAlg = otp.iAlg;
                roles.insert(Entry::AlgorithmRole);
            }

            entry.iOtp.iDigits = otp.iDigits;
            if (otp.iMiniHash) {
                entry.iOtp.iMiniHash = otp.iMiniHash;
            }

            const bool steam = iYubiKeySettings.isSteamHash(entry.iSteamHash);

            if (entry.iSteam != steam) {
                entry.iSteam = steam;
                roles.insert(Entry::SteamRole);
            }

            if (entry.updatePassword()) {
                roles.insert(Entry::PasswordRole);
            }

            if (updateFavorite(entry)) {
                roles.insert(Entry::FavoriteRole);
            }
        }

        if (k >= 0) {
            const Entry& oldEntry = oldList.at(k);

            if (oldEntry.iOp && !oldEntry.iOp->opIsDone()) {
                entry.iOp = oldEntry.iOp;
                opType = oldEntry.iOpType;
                opState = oldEntry.iOpState;
            } else {
                entry.iOp.clear();
            }
        } else {
            entry.iOp.clear();
        }

        if (entry.iOpType != opType) {
            entry.iOpType = opType;
            roles.insert(Entry::EntryOpRole);
        }

        if (entry.iOpState != opState) {
            entry.iOpState = opState;
            roles.insert(Entry::EntryOpStateRole);
        }

        if (entry.iFavorite) {
            setFavoriteMarkedForRefresh(entry.iOpType == EntryOpRefresh);
        }
    }

    // Signal changes for all rows, if there are any changes. Typically,
    // either all of them change (and roles set is not empty) or none
    // (then it's empty and no signal is emitted)
    if (!roles.isEmpty()) {
        const QVector<int> changed(roles.toList().toVector());

        HDEBUG("roles changed" << changed);
        Q_EMIT model->dataChanged(model->index(0), model->index(minCount - 1),
            changed);
    }
}

void
YubiKeyOtpListModel::Private::onYubiKeyUpdatingPasswordsChanged()
{
    if (!iYubiKey->updatingPasswords() && !iFavoriteName.isEmpty()) {
        // Find the favorite entry and fetch its password
        for (QListIterator<Entry> it(iList); it.hasNext();) {
            const Entry& entry = it.next();

            if (entry.iFavorite) {
                HASSERT(entry.iName == iFavoriteName);
                setFavoritePassword(entry.iPassword);
                emitQueuedSignals();
                break;
            }
        }
    }
}

void
YubiKeyOtpListModel::Private::setFavoriteTokenType(
    YubiKeyTokenType aTokenType)
{
    if (iFavoriteTokenType != aTokenType) {
        iFavoriteTokenType = aTokenType;
        queueSignal(SignalFavoriteTokenTypeChanged);
    }
}

void
YubiKeyOtpListModel::Private::setFavoriteMarkedForRefresh(
    bool aMark)
{
    if (iFavoriteMarkedForRefresh != aMark) {
        iFavoriteMarkedForRefresh = aMark;
        queueSignal(SignalFavoriteMarkedForRefreshChanged);
    }
}

void
YubiKeyOtpListModel::Private::setFavoriteName(
    const QString& aName)
{
    if (iFavoriteName != aName) {
        iFavoriteName = aName;
        queueSignal(SignalFavoriteNameChanged);
    }
}

void
YubiKeyOtpListModel::Private::setFavoritePassword(
    const QString& aPassword)
{
    if (iFavoritePassword != aPassword) {
        iFavoritePassword = aPassword;
        HDEBUG(aPassword);
        queueSignal(SignalFavoritePasswordChanged);
    }
}

bool
YubiKeyOtpListModel::Private::updateFavorite(
    Entry& aEntry)
{
    if (iYubiKeySettings.isFavoriteHash(aEntry.iNameHash)) {
        setFavorite(aEntry);
        if (!aEntry.iFavorite) {
            aEntry.iFavorite = true;
            return true;
        }
    } else if (aEntry.iFavorite) {
        aEntry.iFavorite = false;
        clearFavorite();
        return true;
    }
    // The favorite state didn't change
    return false;
}

void
YubiKeyOtpListModel::Private::setFavorite(
    const Entry& aEntry)
{
    setFavoriteName(aEntry.iName);
    setFavoriteTokenType(aEntry.iOtp.iType);
    setFavoriteMarkedForRefresh(aEntry.iOpType == EntryOpRefresh);
    // OTP codes are queried in two steps, with LIST followed by CALCULATE_ALL
    // meaning that in between the password may be empty. Don't apply the
    // empty password in we are in the process of updating it
    if (!aEntry.iPassword.isEmpty() || !iYubiKey->updatingPasswords()) {
        setFavoritePassword(aEntry.iPassword);
    }
}

void
YubiKeyOtpListModel::Private::clearFavorite()
{
    setFavoriteName(QString());
    setFavoritePassword(QString());
    setFavoriteTokenType(YubiKeyTokenType_Unknown);
    setFavoriteMarkedForRefresh(false);
}

/* static */
int
YubiKeyOtpListModel::Private::findName(
    const QList<Entry>& aList,
    const QByteArray& aName)
{
    const int n = aList.count();

    for (int i = 0; i < n; i++) {
        if (aList.at(i).iOtp.iName == aName) {
            return i;
        }
    }
    return -1;
}

inline
int
YubiKeyOtpListModel::Private::findName(
    const QByteArray& aName) const
{
    return findName(iList, aName);
}

int
YubiKeyOtpListModel::Private::findOp(
    YubiKeyOp* aOp) const
{
    if (aOp) {
        const int n = iList.count();

        for (int i = 0; i < n; i++) {
            if (iList.at(i).iOp == aOp) {
                return i;
            }
        }
    }
    return -1;
}

void
YubiKeyOtpListModel::Private::signalEntryChanges(
    int aRow,
    const QSet<int>& aRoles)
{
    if (!aRoles.isEmpty()) {
        YubiKeyOtpListModel* model = parentModel();
        const QModelIndex index = model->index(aRow);
        QVector<int> roles;

        roles.reserve(aRoles.size());
        for (QSetIterator<int> it(aRoles); it.hasNext(); roles.append(it.next()));
        Q_EMIT model->dataChanged(index, index, roles);
    }
}

void
YubiKeyOtpListModel::Private::startOp(
    int aRow,
    YubiKeyOp* aOp,
    EntryOp aOpType,
    const QSet<int>& aRoles)
{
    Entry* entry = &iList[aRow];

    if (entry->iOp) {
        entry->iOp->disconnect(this);
    }
    connect(aOp, SIGNAL(opStateChanged()), SLOT(onEntryOpStateChanged()));
    signalEntryChanges(aRow, entry->setOp(aOp, aOpType, aRoles));
    if (entry->iFavorite) {
        setFavoriteMarkedForRefresh(entry->iOpType == EntryOpRefresh);
    }
}

void
YubiKeyOtpListModel::Private::cancelOp(
    QByteArray aName)
{
    if (iYubiKey) {
        const int row = findName(aName);

        if (row >= 0) {
            Entry* entry = &iList[row];

            if (entry->iOp) {
                entry->iOp->disconnect(this);
                entry->iOp->opCancel();
                iYubiKey->listAndCalculateAll();
                signalEntryChanges(row, entry->setOp(Q_NULLPTR, EntryOpNone));
                if (entry->iFavorite) {
                    setFavoriteMarkedForRefresh(entry->iOpType == EntryOpRefresh);
                }
            }
        }
    }
}

void
YubiKeyOtpListModel::Private::onEntryOpStateChanged()
{
    YubiKeyOp* op = qobject_cast<YubiKeyOp*>(sender());
    const int row = findOp(op);

    if (op->opIsDone()) {
        op->disconnect(this);
    }
    if (row >= 0) {
        signalEntryChanges(row, iList[row].updateOp());
    }
}

void
YubiKeyOtpListModel::Private::deleteToken(
    QByteArray aName)
{
    const int row = findName(aName);

    if (iYubiKey && row >= 0) {
        startOp(row, iYubiKey->deleteToken(aName), EntryOpDelete);
    }
}

void
YubiKeyOtpListModel::Private::refreshToken(
    QByteArray aName)
{
    const int row = findName(aName);

    if (iYubiKey && row >= 0) {
        Entry* entry = &iList[row];

        if (entry->iOpType != EntryOpRefresh) {
            if (entry->iOp) {
                entry->iOp->disconnect(this);
            }
            signalEntryChanges(row, entry->setOp(iYubiKey->
                refreshToken(aName), EntryOpRefresh));
            if (entry->iFavorite) {
                setFavoriteMarkedForRefresh(entry->iOpType == EntryOpRefresh);
            }
        }
    }
}

void
YubiKeyOtpListModel::Private::renameToken(
    QByteArray aFrom,
    const QString& aTo)
{
    const int row = findName(aFrom);

    if (iYubiKey && row >= 0) {
        QSet<int> roles;
        Entry& entry = iList[row];

        if (entry.iNewName != aTo) {
            entry.iNewName = aTo;
            roles.insert(Entry::NewNameRole);
        }

        startOp(row, iYubiKey->renameToken(aFrom,
            YubiKeyUtil::nameToUtf8(aTo)), EntryOpRename, roles);
    }
}

// ==========================================================================
// YubiKeyOtpListModel
// ==========================================================================

YubiKeyOtpListModel::YubiKeyOtpListModel(
    QObject* aParent) :
    QAbstractListModel(aParent),
    iPrivate(new Private(this))
{}

YubiKeyOtpListModel::~YubiKeyOtpListModel()
{
    delete iPrivate;
}

YubiKey*
YubiKeyOtpListModel::yubiKey() const
{
    return iPrivate->iYubiKey.data();
}

void
YubiKeyOtpListModel::setYubiKey(
    YubiKey* aYubiKey)
{
    iPrivate->setYubiKey(aYubiKey);
    iPrivate->emitQueuedSignals();
}

YubiKeyTokenType
YubiKeyOtpListModel::favoriteTokenType() const
{
    return iPrivate->iFavoriteTokenType;
}

bool
YubiKeyOtpListModel::favoriteMarkedForRefresh() const
{
    return iPrivate->iFavoriteMarkedForRefresh;
}

QString
YubiKeyOtpListModel::favoriteName() const
{
    return iPrivate->iFavoriteName;
}

QString
YubiKeyOtpListModel::favoritePassword() const
{
    return iPrivate->iFavoritePassword;
}

bool
YubiKeyOtpListModel::containsName(
    QString aName) const
{
    for (QListIterator<Entry> it(iPrivate->iList); it.hasNext();) {
        if (it.next().iName == aName) {
            return true;
        }
    }
    return false;
}

void
YubiKeyOtpListModel::deleteToken(
    QString aName)
{
    HDEBUG(aName);
    iPrivate->deleteToken(aName.toUtf8());
    iPrivate->emitQueuedSignals();
}

void
YubiKeyOtpListModel::refreshToken(
    QString aName)
{
    HDEBUG(aName);
    iPrivate->refreshToken(YubiKeyUtil::nameToUtf8(aName));
    iPrivate->emitQueuedSignals();
}

void
YubiKeyOtpListModel::renameToken(
    QString aFrom,
    QString aTo)
{
    HDEBUG(aFrom << "=>" << aTo);
    iPrivate->renameToken(aFrom.toUtf8(), aTo);
    iPrivate->emitQueuedSignals();
}

void
YubiKeyOtpListModel::cancelPendingOp(
    QString aName)
{
    HDEBUG(aName);
    iPrivate->cancelOp(aName.toUtf8());
    iPrivate->emitQueuedSignals();
}

Qt::ItemFlags
YubiKeyOtpListModel::flags(
    const QModelIndex& aIndex) const
{
    return QAbstractListModel::flags(aIndex) | Qt::ItemIsEditable;
}

QHash<int,QByteArray>
YubiKeyOtpListModel::roleNames() const
{
    QHash<int,QByteArray> roles;

    #define ROLE(X,x) roles.insert(Entry::X##Role, #x);
    MODEL_ROLES(ROLE)
    #undef ROLE
    return roles;
}

int
YubiKeyOtpListModel::rowCount(
    const QModelIndex&) const
{
    return iPrivate->iList.count();
}

QVariant
YubiKeyOtpListModel::data(
    const QModelIndex& aIndex,
    int aRole) const
{
    const int row = aIndex.row();

    if (row >= 0 && row < iPrivate->iList.count()) {
        return iPrivate->iList.at(row).get(Entry::Role(aRole));
    }
    return QVariant();
}

bool
YubiKeyOtpListModel::setData(
    const QModelIndex& aIndex,
    const QVariant& aValue,
    int aRole)
{
    const int row = aIndex.row();
    bool ok = false;

    if (row >= 0 && row < iPrivate->iList.count()) {
        Entry& data = iPrivate->iList[row];
        QVector<int> roles;
        bool b;

        switch ((Entry::Role)aRole) {
        case Entry::FavoriteRole:
            b = aValue.toBool();
            HDEBUG(row << "favorite" << b);
            if (data.iFavorite != b) {
                roles.append(aRole);
                data.iFavorite = b;
                if (b) {
                    iPrivate->setFavorite(data);
                    iPrivate->iYubiKeySettings.setFavoriteHash(data.iNameHash);
                    // There is only one favorite
                    for (int i = 0; i < iPrivate->iList.count(); i++) {
                        if (i != row) {
                            Entry& other = iPrivate->iList[i];

                            if (other.iFavorite) {
                                other.iFavorite = false;
                                const QModelIndex idx(index(i));
                                Q_EMIT dataChanged(idx, idx, roles);
                                break;
                            }
                        }
                    }
                } else {
                    iPrivate->clearFavorite();
                    iPrivate->iYubiKeySettings.clearFavorite();
                }
                iPrivate->emitQueuedSignals();
            }
            ok = true;
            break;
        case Entry::SteamRole:
            b = aValue.toBool();
            HDEBUG(row << "stream" << b);
            if (data.iSteam != b && (!b || data.canBeSteamToken())) {
                data.iSteam = b;
                roles.append(Entry::SteamRole);
                if (data.updatePassword()) {
                    roles.append(Entry::PasswordRole);
                }
                if (b) {
                    iPrivate->iYubiKeySettings.addSteamHash(data.iSteamHash);
                } else {
                    iPrivate->iYubiKeySettings.removeSteamHash(data.iSteamHash);
                }
            }
            ok = true;
            break;
        case Entry::EntryOpRole:
        case Entry::EntryOpStateRole:
        case Entry::NewNameRole:
        case Entry::NameRole:
        case Entry::TypeRole:
        case Entry::AlgorithmRole:
        case Entry::PasswordRole:
            HDEBUG(row << aRole << "nope");
            break;
        }

        if (!roles.isEmpty()) {
            Q_EMIT dataChanged(aIndex, aIndex, roles);
        }
        iPrivate->emitQueuedSignals();
    }
    return ok;
}

#include "YubiKeyOtpListModel.moc"
