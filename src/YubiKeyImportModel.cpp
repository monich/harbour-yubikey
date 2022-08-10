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

#include "YubiKeyImportModel.h"
#include "YubiKeyTypes.h"
#include "YubiKeyUtil.h"

#include "HarbourBase32.h"
#include "HarbourDebug.h"
#include "HarbourProtoBuf.h"

#include <gutil_misc.h>

#include <foil_input.h>
#include <foil_util.h>

#include <QUrl>

// Model roles
#define MODEL_ROLES_(first,role,last) \
    first(Type,type) \
    role(Algorithm,algorithm) \
    role(Label,label) \
    role(Issuer,issuer) \
    role(Secret,secret) \
    role(Digits,digits) \
    role(Counter,counter) \
    last(Selected,selected)

#define MODEL_ROLES(role) \
    MODEL_ROLES_(role,role,role)

#define TOKEN_TYPE_TOTP "totp"
#define TOKEN_TYPE_HOTP "hotp"

#define TOKEN_ALGORITHM_SHA1 "SHA1"
#define TOKEN_ALGORITHM_SHA256 "SHA256"
#define TOKEN_ALGORITHM_SHA512 "SHA512"

#define TOKEN_KEY_TYPE "type"
#define TOKEN_KEY_LABEL "label"
#define TOKEN_KEY_SECRET "secret"
#define TOKEN_KEY_ISSUER "issuer"
#define TOKEN_KEY_DIGITS "digits"
#define TOKEN_KEY_COUNTER "counter"
#define TOKEN_KEY_ALGORITHM "algorithm"

#define OTPAUTH_SCHEME "otpauth"
#define OTPAUTH_TOTP_PREFIX OTPAUTH_SCHEME "://" TOKEN_TYPE_TOTP "/"
#define OTPAUTH_HOTP_PREFIX OTPAUTH_SCHEME "://" TOKEN_TYPE_HOTP "/"
#define OTPAUTH_MIGRATION_PREFIX   "otpauth-migration://offline?data="

// ==========================================================================
// YubiKeyImportModel::OtpParameters
//
// Represents otpauth-migration payload:
//
// message MigrationPayload {
// enum Algorithm {
//   ALGORITHM_UNSPECIFIED = 0;
//   ALGORITHM_SHA1 = 1;
//   ALGORITHM_SHA256 = 2;
//   ALGORITHM_SHA512 = 3;
//   ALGORITHM_MD5 = 4;  really???
// }
//
// enum DigitCount {
//   DIGIT_COUNT_UNSPECIFIED = 0;
//   DIGIT_COUNT_SIX = 1;
//   DIGIT_COUNT_EIGHT = 2;
// }
//
// enum OtpType {
//   OTP_TYPE_UNSPECIFIED = 0;
//   OTP_TYPE_HOTP = 1;
//   OTP_TYPE_TOTP = 2;
// }
//
// message OtpParameters {
//   bytes secret = 1;
//   string name = 2;
//   string issuer = 3;
//   Algorithm algorithm = 4;
//   DigitCount digits = 5;
//   OtpType type = 6;
//   int64 counter = 7;
// }
//
// repeated OtpParameters otp_parameters = 1;
// int32 version = 2;
// int32 batch_size = 3;
// int32 batch_index = 4;
// int32 batch_id = 5;
// }
//
// ==========================================================================

class YubiKeyImportModel::OtpParameters
{
public:
    enum Algorithm {
        ALGORITHM_UNSPECIFIED,
        ALGORITHM_SHA1,
        ALGORITHM_SHA256,
        ALGORITHM_SHA512,
        ALGORITHM_MD5
    };

    enum DigitCount {
        DIGIT_COUNT_UNSPECIFIED,
        DIGIT_COUNT_SIX,
        DIGIT_COUNT_EIGHT,
    };

    enum OtpType {
        OTP_TYPE_UNSPECIFIED,
        OTP_TYPE_HOTP,
        OTP_TYPE_TOTP
    };

#define DELIMITED_TAG(x) (((x) << HarbourProtoBuf::TYPE_SHIFT) | HarbourProtoBuf::TYPE_DELIMITED)
#define VARINT_TAG(x) (((x) << HarbourProtoBuf::TYPE_SHIFT) | HarbourProtoBuf::TYPE_VARINT)

    static const uchar OTP_PARAMETERS_TAG = DELIMITED_TAG(1);
    static const uchar SECRET_TAG = DELIMITED_TAG(1);
    static const uchar NAME_TAG = DELIMITED_TAG(2);
    static const uchar ISSUER_TAG = DELIMITED_TAG(3);
    static const uchar ALGORITHM_TAG = VARINT_TAG(4);
    static const uchar DIGITS_TAG = VARINT_TAG(5);
    static const uchar TYPE_TAG = VARINT_TAG(6);
    static const uchar COUNTER_TAG = VARINT_TAG(7);

    static const uchar VERSION_TAG = VARINT_TAG(2);
    static const uchar BATCH_SIZE_TAG = VARINT_TAG(3);
    static const uchar BATCH_INDEX_TAG = VARINT_TAG(4);
    static const uchar BATCH_ID_TAG = VARINT_TAG(5);

    static const uchar VERSION = 1;

public:
    OtpParameters();

    bool parse(GUtilRange*);
    void clear();
    bool isValid() const;
    int numDigits() const;
    YubiKeyAlgorithm yubiKeyAlgorithm() const;
    YubiKeyTokenType yubiKeyTokenType() const;

public:
    QByteArray iSecret;
    QString iName;
    QString iIssuer;
    Algorithm iAlgorithm;
    DigitCount iDigits;
    OtpType iType;
    quint64 iCounter;
};

YubiKeyImportModel::OtpParameters::OtpParameters()
{
    clear();
}

void
YubiKeyImportModel::OtpParameters::clear()
{
    iSecret.clear();
    iName.clear();
    iIssuer.clear();
    iAlgorithm = ALGORITHM_SHA1;
    iDigits = DIGIT_COUNT_SIX;
    iType = OTP_TYPE_TOTP;
    iCounter = 0;
}

bool
YubiKeyImportModel::OtpParameters::isValid() const
{
    return !iSecret.isEmpty() &&
        (iAlgorithm == ALGORITHM_SHA1 || iAlgorithm == ALGORITHM_SHA256 ||
         iAlgorithm == ALGORITHM_SHA512) &&
        (iDigits == DIGIT_COUNT_SIX || iDigits == DIGIT_COUNT_EIGHT) &&
        (iType == OTP_TYPE_TOTP || iType == OTP_TYPE_HOTP);
}

int
YubiKeyImportModel::OtpParameters::numDigits() const
{
    switch (iDigits) {
    case DIGIT_COUNT_UNSPECIFIED: break;
    case DIGIT_COUNT_SIX: return 6;
    case DIGIT_COUNT_EIGHT: return 8;
    }
    return 0;
}

YubiKeyAlgorithm
YubiKeyImportModel::OtpParameters::yubiKeyAlgorithm() const
{
    switch (iAlgorithm) {
    case ALGORITHM_MD5: break; // Not supported
    case ALGORITHM_UNSPECIFIED: break;
    case ALGORITHM_SHA1: return YubiKeyAlgorithm_HMAC_SHA1;
    case ALGORITHM_SHA256: return YubiKeyAlgorithm_HMAC_SHA256;
    case ALGORITHM_SHA512:  return YubiKeyAlgorithm_HMAC_SHA512;
    }
    return YubiKeyAlgorithm_Unknown;
}

YubiKeyTokenType
YubiKeyImportModel::OtpParameters::yubiKeyTokenType() const
{
    switch (iType) {
    case OTP_TYPE_UNSPECIFIED: break;
    case OTP_TYPE_HOTP: return YubiKeyTokenType_HOTP;
    case OTP_TYPE_TOTP: return YubiKeyTokenType_TOTP;
    }
    return YubiKeyTokenType_Unknown;
}

bool
YubiKeyImportModel::OtpParameters::parse(
    GUtilRange* aPos)
{
    quint64 tag, value;
    GUtilData payload;
    GUtilRange pos = *aPos;

    clear();

    // Skip leading varints
    while (HarbourProtoBuf::parseVarInt(&pos, &tag) &&
        (tag & HarbourProtoBuf::TYPE_MASK) == HarbourProtoBuf::TYPE_VARINT) {
        if (!HarbourProtoBuf::parseVarInt(&pos, &value)) {
            return false;
        }
    }

    // If parseVarInt failed, tag is zero
    if (tag == OTP_PARAMETERS_TAG &&
        HarbourProtoBuf::parseDelimitedValue(&pos, &payload)) {
        // Looks like OtpParameters
        quint64 value;

        pos.end = (pos.ptr = payload.bytes) + payload.size;
        while (HarbourProtoBuf::parseVarInt(&pos, &tag)) {
            switch (tag) {
            case SECRET_TAG:
                if (HarbourProtoBuf::parseDelimitedValue(&pos, &payload)) {
                    iSecret = YubiKeyUtil::toByteArray(&payload);
                } else {
                    return false;
                }
                break;
            case NAME_TAG:
                if (HarbourProtoBuf::parseDelimitedValue(&pos, &payload)) {
                    iName = QString::fromUtf8((const char*) payload.bytes, payload.size);
                } else {
                    return false;
                }
                break;
            case ISSUER_TAG:
                if (HarbourProtoBuf::parseDelimitedValue(&pos, &payload)) {
                    iIssuer = QString::fromUtf8((const char*) payload.bytes, payload.size);
                } else {
                    return false;
                }
                break;
            case ALGORITHM_TAG:
                if (HarbourProtoBuf::parseVarInt(&pos, &value)) {
                    iAlgorithm = (Algorithm)value;
                } else {
                    return false;
                }
                break;
            case DIGITS_TAG:
                if (HarbourProtoBuf::parseVarInt(&pos, &value)) {
                    iDigits = (DigitCount)value;
                } else {
                    return false;
                }
                break;
            case TYPE_TAG:
                if (HarbourProtoBuf::parseVarInt(&pos, &value)) {
                    iType = (OtpType)value;
                } else {
                    return false;
                }
                break;
            case COUNTER_TAG:
                if (HarbourProtoBuf::parseVarInt(&pos, &value)) {
                    iCounter = value;
                } else {
                    return false;
                }
                break;
            default:
                // Something unintelligible
                return false;
            }
        }
        if (pos.ptr == pos.end && isValid()) {
            // The whole thing has been parsed and looks legit
            aPos->ptr = pos.end;
            return true;
        }
    }
    return false;
}

// ==========================================================================
// YubiKeyImportModel::ModelData
// ==========================================================================

class YubiKeyImportModel::ModelData
{
public:
    typedef QList<ModelData*> List;

    enum Role {
#define FIRST(X,x) FirstRole = Qt::UserRole, X##Role = FirstRole,
#define ROLE(X,x) X##Role,
#define LAST(X,x) X##Role, LastRole = X##Role
        MODEL_ROLES_(FIRST,ROLE,LAST)
#undef FIRST
#undef ROLE
#undef LAST
    };

    ModelData(const YubiKeyToken);
    ModelData(const OtpParameters*);

    QVariant get(Role) const;

public:
    bool iSelected;
    YubiKeyToken iToken;
};

YubiKeyImportModel::ModelData::ModelData(
    const YubiKeyToken aToken) :
    iSelected(true),
    iToken(aToken)
{
}

YubiKeyImportModel::ModelData::ModelData(
    const OtpParameters* aOtpParams) :
    iSelected(true),
    iToken(aOtpParams->yubiKeyTokenType(),
        aOtpParams->yubiKeyAlgorithm(),
        aOtpParams->iName,
        aOtpParams->iIssuer,
        aOtpParams->iSecret,
        aOtpParams->numDigits(),
        (int)aOtpParams->iCounter)
{
}

QVariant
YubiKeyImportModel::ModelData::get(
    Role aRole) const
{
    switch (aRole) {
    case TypeRole: return (int) iToken.type();
    case AlgorithmRole: return (int) iToken.algorithm();
    case LabelRole: return iToken.label();
    case IssuerRole: return iToken.issuer();
    case SecretRole: return iToken.secretBase32();
    case DigitsRole: return iToken.digits();
    case CounterRole: return iToken.counter();
    case SelectedRole: return iSelected;
    }
    return QVariant();
}

// ==========================================================================
// YubiKeyImportModel::Private
// ==========================================================================

class YubiKeyImportModel::Private
{
public:
    Private(YubiKeyImportModel*);
    ~Private();

    ModelData* dataAt(int);
    void setOtpUri(const QString);
    void setItems(const ModelData::List);
    void updateSelectedTokens();

    static YubiKeyToken parseOtpAuthUri(const QByteArray);
    static ModelData::List parseMigrationUri(const QByteArray);
    static ModelData::List parseProtoBuf(gconstpointer, gsize);

public:
    YubiKeyImportModel* iModel;
    ModelData::List iList;
    QList<YubiKeyToken> iSelectedTokens;
    QString iOtpUri;
};

YubiKeyImportModel::Private::Private(
    YubiKeyImportModel* aModel) :
    iModel(aModel)
{
}

YubiKeyImportModel::Private::~Private()
{
    qDeleteAll(iList);
}

inline
YubiKeyImportModel::ModelData*
YubiKeyImportModel::Private::dataAt(
    int aIndex)
{
    return (aIndex >= 0 && aIndex < iList.count()) ?
        iList.at(aIndex) : Q_NULLPTR;
}

YubiKeyImportModel::ModelData::List
YubiKeyImportModel::Private::parseProtoBuf(
    gconstpointer aData,
    gsize aSize)
{
    ModelData::List list;
    OtpParameters otp;
    GUtilRange pos;

    pos.end = (pos.ptr = (const guint8*) aData) + aSize;
    while (otp.parse(&pos)) {
        list.append(new ModelData(&otp));
    }
    return list;
}

YubiKeyImportModel::ModelData::List
YubiKeyImportModel::Private::parseMigrationUri(
    const QByteArray aUri)
{
    GUtilRange pos;
    GUtilData prefixBytes;
    ModelData::List list;

    pos.end = (pos.ptr = (const guint8*)aUri.constData()) + aUri.size();
    if (gutil_range_skip_prefix(&pos, gutil_data_from_string(&prefixBytes,
        OTPAUTH_MIGRATION_PREFIX))) {
        // QByteArray is NULL-terminated
        char* unescaped = g_uri_unescape_string((char*)pos.ptr, NULL);

        if (unescaped) {
            pos.end = (pos.ptr = (const guint8*) unescaped) + strlen(unescaped);

            GBytes* bytes = foil_parse_base64(&pos, FOIL_INPUT_BASE64_VALIDATE);
            if (bytes) {
                gsize size;
                gconstpointer data = g_bytes_get_data(bytes, &size);

#if HARBOUR_DEBUG
                pos.end = (pos.ptr = (const guint8*) data) + size;
                HDEBUG("Decoded" << size << "bytes");
                while (pos.ptr < pos.end) {
                    char line[GUTIL_HEXDUMP_BUFSIZE];

                    pos.ptr += gutil_hexdump(line, pos.ptr, pos.end - pos.ptr);
                    HDEBUG(line);
                }
#endif // HARBOUR_DEBUG

                list = parseProtoBuf(data, size);
                HDEBUG(list.count() << "tokens");
                g_bytes_unref(bytes);
            }
            g_free(unescaped);
        }
    }
    return list;
}

YubiKeyToken
YubiKeyImportModel::Private::parseOtpAuthUri(
    const QByteArray aUri)
{
    YubiKeyTokenType type = YubiKeyTokenType_Unknown;
    GUtilData prefixBytes;
    GUtilRange pos;

    // Check scheme + type prefix
    pos.end = (pos.ptr = (const guint8*)aUri.constData()) + aUri.size();
    gutil_data_from_string(&prefixBytes, OTPAUTH_TOTP_PREFIX);

    if (gutil_range_skip_prefix(&pos, &prefixBytes)) {
        type = YubiKeyTokenType_TOTP;
    } else {
        gutil_data_from_string(&prefixBytes, OTPAUTH_HOTP_PREFIX);
        if (gutil_range_skip_prefix(&pos, &prefixBytes)) {
            type = YubiKeyTokenType_HOTP;
        }
    }

    if (type != YubiKeyTokenType_Unknown) {
        QByteArray label, secret, issuer, algorithm, digits, counter;

        while (pos.ptr < pos.end && pos.ptr[0] != '?') {
            label.append(*pos.ptr++);
        }

        GUtilData secretTag;
        GUtilData issuerTag;
        GUtilData digitsTag;
        GUtilData counterTag;
        GUtilData algorithmTag;

        gutil_data_from_string(&secretTag, TOKEN_KEY_SECRET "=");
        gutil_data_from_string(&issuerTag, TOKEN_KEY_ISSUER "=");
        gutil_data_from_string(&digitsTag, TOKEN_KEY_DIGITS "=");
        gutil_data_from_string(&counterTag, TOKEN_KEY_COUNTER "=");
        gutil_data_from_string(&algorithmTag, TOKEN_KEY_ALGORITHM "=");

        while (pos.ptr < pos.end) {
            pos.ptr++;

            QByteArray* value =
                gutil_range_skip_prefix(&pos, &secretTag) ? &secret :
                gutil_range_skip_prefix(&pos, &issuerTag) ? &issuer :
                gutil_range_skip_prefix(&pos, &digitsTag) ? &digits :
                gutil_range_skip_prefix(&pos, &counterTag) ? &counter :
                gutil_range_skip_prefix(&pos, &algorithmTag) ? &algorithm :
                Q_NULLPTR;

            if (value) {
                value->truncate(0);
                while (pos.ptr < pos.end && pos.ptr[0] != '&') {
                    value->append(*pos.ptr++);
                }
            } else {
                while (pos.ptr < pos.end && pos.ptr[0] != '&') {
                    pos.ptr++;
                }
            }
        }

        if (!secret.isEmpty()) {
            const QByteArray bytes(HarbourBase32::
                fromBase32(QUrl::fromPercentEncoding(secret)));

            if (!bytes.isEmpty()) {
                const YubiKeyAlgorithm alg = algorithm.isEmpty() ?
                    YubiKeyAlgorithm_Default : YubiKeyUtil::
                    algorithmFromName(QString::fromLatin1(algorithm));

                if (alg != YubiKeyAlgorithm_Unknown) {
                    int dig = YubiKeyToken::DefaultDigits;
                    int imf = 0;

                    if (!digits.isEmpty()) {
                        bool ok;
                        const int n = digits.toInt(&ok);

                        if (ok &&
                            n >= YubiKeyToken::MinDigits &&
                            n <= YubiKeyToken::MaxDigits) {
                            dig = n;
                        }
                    }

                    if (!counter.isEmpty()) {
                        bool ok;
                        const int n = counter.toInt(&ok);

                        if (ok && n >= 0) {
                            imf = n;
                        }
                    }

                    return YubiKeyToken(type, alg,
                        QUrl::fromPercentEncoding(label),
                        QUrl::fromPercentEncoding(issuer),
                        bytes, dig, imf);
                }
            }
        }
    }
    return YubiKeyToken();
}

void
YubiKeyImportModel::Private::setOtpUri(
    const QString aOtpUri)
{
    if (iOtpUri != aOtpUri) {
        iOtpUri = aOtpUri;

        const QByteArray uri(iOtpUri.trimmed().toUtf8());
        HDEBUG(uri.constData());

        ModelData::List list;
        const YubiKeyToken singleToken(parseOtpAuthUri(uri));

        if (singleToken.valid()) {
            list.append(new ModelData(singleToken));
            HDEBUG("single token" << singleToken);
        } else {
            list = parseMigrationUri(uri);
        }

        if (!list.isEmpty()) {
            setItems(list);
        } else if (iList.count() > 0) {
            iModel->beginRemoveRows(QModelIndex(), 0, iList.count() - 1);
            qDeleteAll(iList);
            iList.clear();
            iModel->endRemoveRows();
        }

        Q_EMIT iModel->otpUriChanged();
    }
}

void
YubiKeyImportModel::Private::setItems(
    const ModelData::List aList)
{
    const int prevCount = iList.count();
    const int newCount = aList.count();
    const int changed = qMin(prevCount, newCount);
    const QList<YubiKeyToken> prevSelectedTokens(iSelectedTokens);

    if (newCount < prevCount) {
        iModel->beginRemoveRows(QModelIndex(), newCount, prevCount - 1);
        qDeleteAll(iList);
        iList = aList;
        iModel->endRemoveRows();
    } else if (newCount > prevCount) {
        iModel->beginInsertRows(QModelIndex(), prevCount, newCount - 1);
        qDeleteAll(iList);
        iList = aList;
        iModel->endInsertRows();
    } else {
        qDeleteAll(iList);
        iList = aList;
    }

    updateSelectedTokens();
    if (prevSelectedTokens != iSelectedTokens) {
        if (!prevSelectedTokens.count() != !iSelectedTokens.count()) {
            Q_EMIT iModel->haveSelectedTokensChanged();
        }
        Q_EMIT iModel->selectedTokensChanged();
    }
    if (changed > 0) {
        Q_EMIT iModel->dataChanged(iModel->index(0), iModel->index(changed - 1));
    }
}

void
YubiKeyImportModel::Private::updateSelectedTokens()
{
    QList<YubiKeyToken> list;
    const int n = iList.count();

    for (int i = 0; i < n; i++) {
        const ModelData* entry = iList.at(i);

        if (entry->iSelected) {
            list.append(entry->iToken);
        }
    }

    HDEBUG("selected" << list);
    iSelectedTokens = list;
}

// ==========================================================================
// YubiKeyImportModel
// ==========================================================================

YubiKeyImportModel::YubiKeyImportModel(
    QObject* aParent) :
    QAbstractListModel(aParent),
    iPrivate(new Private(this))
{
    connect(this, SIGNAL(modelReset()), SIGNAL(countChanged()));
    connect(this, SIGNAL(rowsInserted(QModelIndex,int,int)), SIGNAL(countChanged()));
    connect(this, SIGNAL(rowsRemoved(QModelIndex,int,int)), SIGNAL(countChanged()));
    qRegisterMetaType<YubiKeyToken>();
    qRegisterMetaType<QList<YubiKeyToken> >();
}

YubiKeyImportModel::~YubiKeyImportModel()
{
    delete iPrivate;
}

QString
YubiKeyImportModel::otpUri() const
{
    return iPrivate->iOtpUri;
}

void
YubiKeyImportModel::setOtpUri(
    const QString aOtpUri)
{
    iPrivate->setOtpUri(aOtpUri);
}

YubiKeyToken
YubiKeyImportModel::getToken(
    int aIndex) const
{
    const ModelData* entry = iPrivate->dataAt(aIndex);

    return entry ? entry->iToken : YubiKeyToken();
}

QList<YubiKeyToken>
YubiKeyImportModel::selectedTokens() const
{
    return iPrivate->iSelectedTokens;
}

bool
YubiKeyImportModel::haveSelectedTokens() const
{
    return !iPrivate->iSelectedTokens.isEmpty();
}

Qt::ItemFlags
YubiKeyImportModel::flags(
    const QModelIndex& aIndex) const
{
    return QAbstractListModel::flags(aIndex) | Qt::ItemIsEditable;
}

QHash<int,QByteArray>
YubiKeyImportModel::roleNames() const
{
    QHash<int,QByteArray> roles;
#define ROLE(X,x) roles.insert(ModelData::X##Role, #x);
MODEL_ROLES(ROLE)
#undef ROLE
    return roles;
}

int
YubiKeyImportModel::rowCount(
    const QModelIndex& aParent) const
{
    return iPrivate->iList.count();
}

QVariant
YubiKeyImportModel::data(
    const QModelIndex& aIndex,
    int aRole) const
{
    const ModelData* entry = iPrivate->dataAt(aIndex.row());

    return entry ? entry->get((ModelData::Role)aRole) : QVariant();
}

bool
YubiKeyImportModel::setData(
    const QModelIndex& aIndex,
    const QVariant& aValue,
    int aRole)
{
    ModelData* entry = iPrivate->dataAt(aIndex.row());
    bool ok = false;

    if (entry) {
        QVector<int> roles;
        QString s;
        bool b;
        int i;

        switch ((ModelData::Role)aRole) {
        case ModelData::TypeRole:
            i = aValue.toInt(&ok);
            if (ok) {
                const YubiKeyTokenType type = YubiKeyUtil::validType(i);

                if (type != YubiKeyTokenType_Unknown) {
                    HDEBUG(aIndex.row() << "type" << type);
                    if (entry->iToken.type() != type) {
                        entry->iToken = entry->iToken.withType(type);
                        roles.append(aRole);
                    }
                } else {
                    ok = false;
                }
            }
            break;

        case ModelData::AlgorithmRole:
            i = aValue.toInt(&ok);
            if (ok) {
                const YubiKeyAlgorithm alg = YubiKeyUtil::validAlgorithm(i);

                if (alg != YubiKeyAlgorithm_Unknown) {
                    HDEBUG(aIndex.row() << "algorithm" << alg);
                    if (entry->iToken.algorithm() != alg) {
                        entry->iToken = entry->iToken.withAlgorithm(alg);
                        roles.append(aRole);
                    }
                } else {
                    ok = false;
                }
            }
            break;

        case ModelData::LabelRole:
            s = aValue.toString();
            HDEBUG(aIndex.row() << "label" << s);
            if (entry->iToken.label() != s) {
                entry->iToken = entry->iToken.withLabel(s);
                roles.append(aRole);
            }
            ok = true;
            break;

        case ModelData::IssuerRole:
            s = aValue.toString();
            HDEBUG(aIndex.row() << "issuer" << s);
            if (entry->iToken.issuer() != s) {
                entry->iToken = entry->iToken.withIssuer(s);
                roles.append(aRole);
            }
            ok = true;
            break;

        case ModelData::SecretRole:
            s = aValue.toString().trimmed().toLower();
            ok = HarbourBase32::isValidBase32(s);
            if (ok) {
                const QByteArray secret(HarbourBase32::fromBase32(s));

                HDEBUG(aIndex.row() << "secret" << s);
                if (entry->iToken.secret() != secret) {
                    entry->iToken = entry->iToken.withSecret(secret);
                    roles.append(aRole);
                }
            }
            break;

        case ModelData::DigitsRole:
            i = aValue.toInt(&ok);
            if (ok) {
                if (i >= YubiKeyToken::MinDigits &&
                    i <= YubiKeyToken::MaxDigits) {
                    HDEBUG(aIndex.row() << "digits" << i);
                    if (entry->iToken.digits() != i) {
                        entry->iToken = entry->iToken.withDigits(i);
                        roles.append(aRole);
                    }
                } else {
                    ok = false;
                }
            }
            break;

        case ModelData::CounterRole:
            i = aValue.toInt(&ok);
            if (ok) {
                HDEBUG(aIndex.row() << "counter" << i);
                if (entry->iToken.counter() != i) {
                    entry->iToken = entry->iToken.withCounter(i);
                    roles.append(aRole);
                }
            }
            break;

        case ModelData::SelectedRole:
            b = aValue.toBool();
            HDEBUG(aIndex.row() << "selected" << b);
            if (entry->iSelected != b) {
                entry->iSelected = b;
                roles.append(aRole);

                // Selection has changed, update the list of selected tokens
                const bool hadSelectedTokens = haveSelectedTokens();
                iPrivate->updateSelectedTokens();
                if (hadSelectedTokens != haveSelectedTokens()) {
                    Q_EMIT haveSelectedTokensChanged();
                }
                // If the entry is selected, the signal will be emitted from
                // common place below
                if (!entry->iSelected) {
                    Q_EMIT selectedTokensChanged();
                }
            }
            ok = true;
            break;
        }

        if (!roles.isEmpty()) {
            if (entry->iSelected) {
                iPrivate->updateSelectedTokens();
                Q_EMIT selectedTokensChanged();
            }
            Q_EMIT dataChanged(aIndex, aIndex, roles);
        }
    }
    return ok;
}

void
YubiKeyImportModel::setToken(
    int aRow,
    int aType,
    int aAlgorithm,
    const QString aLabel,
    const QString aIssuer,
    const QString aSecretBase32,
    int aDigits,
    int aCounter)
{
    ModelData* entry = iPrivate->dataAt(aRow);

    if (entry) {
        QVector<int> roles;
        const YubiKeyTokenType type = YubiKeyUtil::validType(aType);
        const YubiKeyAlgorithm alg = YubiKeyUtil::validAlgorithm(aAlgorithm);
        const QString secret(aSecretBase32.trimmed());

        if (type != YubiKeyTokenType_Unknown &&
            type != entry->iToken.type()) {
            entry->iToken = entry->iToken.withType(type);
            HDEBUG(aRow << "type" << type);
            roles.append(ModelData::TypeRole);
        }

        if (alg != YubiKeyAlgorithm_Unknown &&
            alg != entry->iToken.algorithm()) {
            entry->iToken = entry->iToken.withAlgorithm(alg);
            HDEBUG(aRow << "algorithm" << alg);
            roles.append(ModelData::AlgorithmRole);
        }

        if (entry->iToken.label() != aLabel) {
            entry->iToken = entry->iToken.withLabel(aLabel);
            HDEBUG(aRow << "label" << aLabel);
            roles.append(ModelData::LabelRole);
        }

        if (entry->iToken.issuer() != aIssuer) {
            entry->iToken = entry->iToken.withIssuer(aIssuer);
            HDEBUG(aRow << "issuer" << aIssuer);
            roles.append(ModelData::IssuerRole);
        }

        if (HarbourBase32::isValidBase32(secret)) {
            const QByteArray bytes(HarbourBase32::fromBase32(secret));

            if (entry->iToken.secret() != bytes) {
                entry->iToken = entry->iToken.withSecret(bytes);
                HDEBUG(aRow << "secret" << entry->iToken.secretBase32());
                roles.append(ModelData::SecretRole);
            }
        }

        if (aDigits != entry->iToken.digits() &&
            aDigits >= YubiKeyToken::MinDigits &&
            aDigits <= YubiKeyToken::MaxDigits) {
            entry->iToken = entry->iToken.withDigits(aDigits);
            HDEBUG(aRow << "digits" << aDigits);
            roles.append(ModelData::DigitsRole);
        }

        if (entry->iToken.counter() != aCounter) {
            entry->iToken = entry->iToken.withCounter(aCounter);
            HDEBUG(aRow << "counter" << aCounter);
            roles.append(ModelData::CounterRole);
        }

        if (!roles.isEmpty()) {
            if (entry->iSelected) {
                iPrivate->updateSelectedTokens();
                Q_EMIT selectedTokensChanged();
            }
            const QModelIndex idx(index(aRow));
            Q_EMIT dataChanged(idx, idx, roles);
        }
    }
}
