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

#include "YubiKeyAuthDataModel.h"

#include "YubiKeyUtil.h"

// Model roles
#define MODEL_ROLES_(first,last) \
    first(YubiKeyId,yubiKeyId) \
    last(LastAccess,lastAccess)

#define MODEL_ROLES(role) \
    MODEL_ROLES_(role,role)

// ==========================================================================
// YubiKeyAuthDataModel::Private
// ==========================================================================

class YubiKeyAuthDataModel::Private
{
public:
    enum Role {
        #define FIRST(X,x) FirstRole = Qt::UserRole, X##Role = FirstRole,
        #define LAST(X,x) X##Role, LastRole = X##Role
        MODEL_ROLES_(FIRST,LAST)
        #undef ROLE
    };

    // QtCreator syntax highlighter gets confused by the above macro magic.
    // Somehow this stupid enum unconfuses it :/
    enum { _ };

    Private();

    static QList<YubiKeyAuth> sort(QList<YubiKeyAuth>);
    static bool accessTimeLessThan(const YubiKeyAuth&, const YubiKeyAuth&);

public:
    QList<YubiKeyAuth> iList;
};

YubiKeyAuthDataModel::Private::Private() :
    iList(sort(YubiKeyAuth::all()))
{}

// static
QList<YubiKeyAuth>
YubiKeyAuthDataModel::Private::sort(
    QList<YubiKeyAuth> aList)
{
    QList<YubiKeyAuth> list(aList);

    std::sort(list.begin(), list.end(), accessTimeLessThan);
    return list;
}

// static
bool
YubiKeyAuthDataModel::Private::accessTimeLessThan(
    const YubiKeyAuth& aAuth1,
    const YubiKeyAuth& aAuth2)
{
    return aAuth1.lastAccessTime() > aAuth2.lastAccessTime();
}

// ==========================================================================
// YubiKeyAuthDataModel
// ==========================================================================


YubiKeyAuthDataModel::YubiKeyAuthDataModel(
    QObject* aParent) :
    QAbstractListModel(aParent),
    iPrivate(new Private())
{}

YubiKeyAuthDataModel::~YubiKeyAuthDataModel()
{
    delete iPrivate;
}

QHash<int,QByteArray>
YubiKeyAuthDataModel::roleNames() const
{
    QHash<int,QByteArray> roles;

    #define ROLE(X,x) roles.insert(Private::X##Role, #x);
    MODEL_ROLES(ROLE)
    #undef ROLE
    return roles;
}

int
YubiKeyAuthDataModel::rowCount(
    const QModelIndex&) const
{
    return iPrivate->iList.count();
}

QVariant
YubiKeyAuthDataModel::data(
    const QModelIndex& aIndex,
    int aRole) const
{
    const int row = aIndex.row();

    if (row >= 0 && row < iPrivate->iList.count()) {
        const YubiKeyAuth& auth = iPrivate->iList.at(row);

        switch (Private::Role(aRole)) {
        case Private::YubiKeyIdRole:
            return QString(auth.yubiKeyId().toHex());
        case Private::LastAccessRole:
            return auth.lastAccessTime();
        }
    }
    return QVariant();
}

void
YubiKeyAuthDataModel::refresh()
{
    QList<YubiKeyAuth> newList(Private::sort(YubiKeyAuth::all()));
    const int prevCount = iPrivate->iList.count();

    if (iPrivate->iList != newList) {
        const int newCount = newList.count();
        const int changed = qMin(prevCount, newCount);

        if (newCount < prevCount) {
            beginRemoveRows(QModelIndex(), newCount, prevCount - 1);
            iPrivate->iList = newList;
            endRemoveRows();
        } else if (newCount > prevCount) {
            beginInsertRows(QModelIndex(), prevCount, newCount - 1);
            iPrivate->iList = newList;
            endInsertRows();
        } else {
            iPrivate->iList = newList;
        }
        if (changed > 0) {
            Q_EMIT dataChanged(index(0), index(changed - 1));
        }
    } else if (prevCount > 0) {
        // Update all access times, just in case
        Q_EMIT dataChanged(index(0), index(prevCount - 1));
    }
}

bool
YubiKeyAuthDataModel::remove(
    QString aYubiKeyId)
{
    const QByteArray id(YubiKeyUtil::fromHex(aYubiKeyId));

    if (!id.isEmpty()) {
        const int n = iPrivate->iList.count();

        for (int i = 0; i < n; i++) {
            YubiKeyAuth auth(iPrivate->iList.at(i));

            if (auth.yubiKeyId() == id) {
                beginRemoveRows(QModelIndex(), i, i);
                iPrivate->iList.removeAt(i);
                auth.forgetPassword();
                endRemoveRows();
                return true;
            }
        }
    }
    return false;
}
