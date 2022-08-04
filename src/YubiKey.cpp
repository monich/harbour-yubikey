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

#include <nfcdc_default_adapter.h>
#include <nfcdc_isodep.h>

#include <foil_random.h>

#include "YubiKey.h"
#include "YubiKeyAuth.h"
#include "YubiKeyConstants.h"
#include "YubiKeyTag.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"

#include <QAtomicInt>
#include <QDateTime>
#include <QMap>
#include <QTimer>

// s(SignalName,signalName)
#define YUBIKEY_SIGNALS(s) \
    s(YubiKeyVersion,yubiKeyVersion) \
    s(OtpListFetched,otpListFetched) \
    s(OtpList,otpList) \
    s(OtpData,otpData) \
    s(Present,present) \
    s(AuthAccess,authAccess) \
    s(OperationIds,operationIds) \
    s(TotpTimeLeft,totpTimeLeft) \
    s(TotpValid,totpValid)

#if HARBOUR_DEBUG
#  define REPORT_ERROR(name,sw,err) \
    ((void)((err) ? HDEBUG(name " error" << (err)->message) : \
    HDEBUG(name " error" << hex << sw)))
#  define DUMP_CMD(name,cmd,id) ((id) ? (HDEBUG(name << \
    qPrintable(YubiKeyUtil::toHex(&(cmd).data)) << id)) : \
    (HDEBUG(name << "- oops!")))
#else
#  define REPORT_ERROR(name,sw,err) ((void)0)
#  define DUMP_CMD(name,cmd,id) ((void)0)
#endif // HARBOUR_DEBUG

// ==========================================================================
// YubiKey::Private
// ==========================================================================

#define CALCULATE_ALL_RESP_METHOD calculateAllResp
#define RESET_RESP_METHOD resetResp
#define SET_CODE_RESP_METHOD setCodeResp
#define REMOVE_CODE_RESP_METHOD removeCodeResp
#define PUT_RESP_METHOD putResp

class YubiKey::Private :
    public QObject,
    public YubiKeyConstants
{
    Q_OBJECT

public:
    typedef void (YubiKey::*SignalEmitter)();
    typedef uint SignalMask;

    enum {
        MaxNameLen = 64,
        MinKeySize = 14
    };

    enum Signal {
        // The order must match the initializer in emitQueuedSignals()
#define SIGNAL_ENUM_(Name,name) Signal##Name##Changed,
        YUBIKEY_SIGNALS(SIGNAL_ENUM_)
#undef  SIGNAL_ENUM_
        SignalAccessKeyNotAccepted,
        SignalCount
    };

    struct CalculateAllApdu {
        NfcIsoDepApdu iApdu;
        uchar iData[10];
    };

    class KeyOperation : public YubiKeyTag::Operation {
    public:
        KeyOperation(const char*, YubiKey*, bool aRequireSelect = false);

    protected:
        ~KeyOperation();

    protected:
        YubiKey* const iYubiKey;
    };

    // Authorize (verify)
    class AuthorizeOperation : public KeyOperation {
    public:
        AuthorizeOperation(YubiKey*, bool);

    protected:
        bool startOperation() Q_DECL_OVERRIDE;

    private:
        static void validateResp(Operation*, const GUtilData*, guint, const GError*);

    private:
        YubiKeyAlgorithm iAlgorithm;
        QByteArray iHostChallenge;
        QByteArray iAccessKey;
    };

    // List
    class ListOperation : public KeyOperation {
    public:
        ListOperation(YubiKey*);

    protected:
        bool startOperation() Q_DECL_OVERRIDE;

    private:
        static void listResp(Operation*, const GUtilData*, guint, const GError*);
        static void calculateAllResp(Operation*, const GUtilData*, guint, const GError*);

    private:
        QByteArray iRespBuf;
    };

    // Refresh (calculate individual codes)
    class RefreshOperation : public KeyOperation {
    public:
        RefreshOperation(YubiKey*, const QStringList);

    protected:
        bool startOperation() Q_DECL_OVERRIDE;

    private:
        bool calculateNext();
        static void calculateResp(Operation*, const GUtilData*, guint, const GError*);

    private:
        const QStringList iNames;
        QByteArray iLastNameUtf8;
        int iNextName;
    };

    // Delete
    class DeleteOperation : public KeyOperation {
    public:
        DeleteOperation(YubiKey*, const QStringList);

    protected:
        bool startOperation() Q_DECL_OVERRIDE;

    private:
        bool deleteNext();
        static void deleteResp(Operation*, const GUtilData*, guint, const GError*);

    private:
        const QStringList iNames;
        int iNextName;
    };

public:
    static QMap<QByteArray,YubiKey*> gMap;

    static const NfcIsoDepApdu CMD_LIST;
    static const NfcIsoDepApdu CMD_RESET;
    static const NfcIsoDepApdu CMD_VALIDATE_TEMPLATE;
    static const NfcIsoDepApdu CMD_CALCULATE_ALL_TEMPLATE;
    static const NfcIsoDepApdu CMD_CALCULATE_TEMPLATE;
    static const NfcIsoDepApdu CMD_SET_CODE;
    static const NfcIsoDepApdu CMD_PUT_TEMPLATE;
    static const NfcIsoDepApdu CMD_DELETE_TEMPLATE;
    static const NfcIsoDepApdu CMD_SEND_REMAINING;

    static const uchar CMD_SET_CODE_DATA[];

    Private(const QByteArray, YubiKey*);
    ~Private();

    YubiKey* parentObject();
    void queueSignal(Signal);
    void emitQueuedSignals();

    void dropTag();
    bool validTag();
    bool canTransmit();
    bool haveAccess();
    void verifyAuthorization();
    void recheckAuthorization();
    void authorized(YubiKeyAuthAccess);
    void submit(YubiKeyTag::Operation*, bool aForceSelect = false);
    void submitUnique(YubiKeyTag::Operation*, bool aForceSelect = false);
    void requestList(bool aRightAway = false);
    void submitCalculateAll();
    int submitPutHotpToken(YubiKeyAlgorithm, const QString, const QByteArray, int, int);
    int submitPutTotpToken(YubiKeyAlgorithm, const QString, const QByteArray, int);
    int setPassword(const QString);
    bool submitPassword(const QString, bool);
    int reset();
    void updateTagState();
    void updateTotpTimer();
    void updatePath();
    void updateAuthAccess(YubiKeyAuthAccess);
    void updateAuthAlgorithm();
    void updateYubiKeyVersion();
    void updateOperationIds();
    void calculateAllOk(const QByteArray);
    void updateOtpList(const QByteArray);
    void updateOtpData(const QByteArray);
    void setCodeResp(const GUtilData*, guint, const GError*, SignalEmitter);
    void mergeOtpCode(const QByteArray, const GUtilData*);
    const NfcIsoDepApdu* buildCalculateAllApdu(CalculateAllApdu*);

    static qint64 currentPeriod();
    static void staticAdapterEvent(NfcDefaultAdapter*, NFC_DEFAULT_ADAPTER_PROPERTY, void*);
    static QByteArray nameToUtf8(const QString);

public Q_SLOTS:
    YUBIKEY_TRANSMIT_RESP_SLOT(CALCULATE_ALL_RESP_METHOD);
    YUBIKEY_TRANSMIT_RESP_SLOT(RESET_RESP_METHOD);
    YUBIKEY_TRANSMIT_RESP_SLOT(SET_CODE_RESP_METHOD);
    YUBIKEY_TRANSMIT_RESP_SLOT(REMOVE_CODE_RESP_METHOD);
    YUBIKEY_TRANSMIT_RESP_SLOT(PUT_RESP_METHOD);
    void onTagStateChanged();
    void onYubiKeyIdChanged();
    void onYubiKeyVersionChanged();
    void onYubiKeyAuthAlgorithmChanged();
    void onYubiKeyAuthChallengeChanged();
    void onOperationIdsChanged();
    void onAccessKeyChanged(const QByteArray, YubiKeyAlgorithm);
    void onTotpTimer();

public:
    QAtomicInt iRef;
    SignalMask iQueuedSignals;
    Signal iFirstQueuedSignal;
    YubiKeyTag* iTag;
    YubiKeyAuth* iAuth;
    YubiKeyAlgorithm iAuthAlgorithm;
    NfcDefaultAdapter* iAdapter;
    gulong iAdapterEventId;
    GCancellable* iCancel;
    bool iPresent;
    YubiKeyAuthAccess iAuthAccess;
    const QByteArray iYubiKeyId;
    const QString iYubiKeyIdString;
    QByteArray iYubiKeyVersion;
    QString iYubiKeyVersionString;
    QByteArray iOtpList;
    QString iOtpListString;
    QByteArray iOtpData;
    QString iOtpDataString;
    QList<int> iOperationIds;
    bool iOtpListFetched;
    QTimer* iTotpTimer;
    qint64 iLastRequestedPeriod;    // seconds
    qint64 iLastReceivedPeriod;     // seconds
    int iTotpTimeLeft;              // seconds
    bool iTotpValid;
};

QMap<QByteArray,YubiKey*> YubiKey::Private::gMap;

// https://developers.yubico.com/OATH/YKOATH_Protocol.html
const NfcIsoDepApdu YubiKey::Private::CMD_LIST = {
    0x00, 0xa1, 0x00, 0x00, { NULL, 0 }, 0
};
const NfcIsoDepApdu YubiKey::Private::CMD_RESET = {
    0x00, 0x04, 0xde, 0xad, { NULL, 0 }, 0
};
const NfcIsoDepApdu YubiKey::Private::CMD_VALIDATE_TEMPLATE = {
    0x00, 0xa3, 0x00, 0x00, { NULL, 0 }, 0
};
const NfcIsoDepApdu YubiKey::Private::CMD_CALCULATE_ALL_TEMPLATE = {
    0x00, 0xa4, 0x00, 0x00, { NULL, 0 }, 0
};
const NfcIsoDepApdu YubiKey::Private::CMD_CALCULATE_TEMPLATE = {
    0x00, 0xa2, 0x00, 0x00, { NULL, 0 }, 0
};
#define CMD_DATA(data) {data, sizeof(data)}
const uchar YubiKey::Private::CMD_SET_CODE_DATA[] = { TLV_TAG_KEY, 0x00 };
const NfcIsoDepApdu YubiKey::Private::CMD_SET_CODE = {
    0x00, 0x03, 0x00, 0x00, CMD_DATA(CMD_SET_CODE_DATA), 0
};
const NfcIsoDepApdu YubiKey::Private::CMD_PUT_TEMPLATE = {
    0x00, 0x01, 0x00, 0x00, { NULL, 0 }, 0
};
const NfcIsoDepApdu YubiKey::Private::CMD_DELETE_TEMPLATE = {
    0x00, 0x02, 0x00, 0x00, { NULL, 0 }, 0
};
const NfcIsoDepApdu YubiKey::Private::CMD_SEND_REMAINING = {
    0x00, 0xa5, 0x00, 0x00, { NULL, 0 }, 0
};

YubiKey::Private::Private(
    const QByteArray aYubiKeyId,
    YubiKey* aParent) :
    QObject(aParent),
    iRef(1),
    iQueuedSignals(0),
    iFirstQueuedSignal(SignalCount),
    iTag(Q_NULLPTR),
    iAuth(YubiKeyAuth::get()),
    iAuthAlgorithm(YubiKeyAlgorithm_Unknown),
    iAdapter(nfc_default_adapter_new()),
    iCancel(g_cancellable_new()),
    iPresent(false),
    iAuthAccess(YubiKeyAuthAccessUnknown),
    iYubiKeyId(aYubiKeyId),
    iYubiKeyIdString(YubiKeyUtil::toHex(aYubiKeyId)),
    iOtpListFetched(false),
    iTotpTimer(new QTimer(this)),
    iLastRequestedPeriod(0),
    iLastReceivedPeriod(0),
    iTotpTimeLeft(0),
    iTotpValid(false)
{
    iTotpTimer->setSingleShot(true);
    connect(iTotpTimer, SIGNAL(timeout()), SLOT(onTotpTimer()));
    connect(iAuth,
        SIGNAL(accessKeyChanged(QByteArray,YubiKeyAlgorithm)),
        SLOT(onAccessKeyChanged(QByteArray,YubiKeyAlgorithm)));
    iAdapterEventId = nfc_default_adapter_add_property_handler(iAdapter,
        NFC_DEFAULT_ADAPTER_PROPERTY_TAGS, staticAdapterEvent, this);
}

YubiKey::Private::~Private()
{
    nfc_default_adapter_remove_handler(iAdapter, iAdapterEventId);
    nfc_default_adapter_unref(iAdapter);
    dropTag();
    iAuth->disconnect(this);
    iAuth->put();
}

inline
YubiKey*
YubiKey::Private::parentObject()
{
    return qobject_cast<YubiKey*>(parent());
}

void
YubiKey::Private::queueSignal(
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
YubiKey::Private::emitQueuedSignals()
{
    static const char* signalName [] = {
        // The order must match the Signal enum
#define SIGNAL_NAME_(Name,name) G_STRINGIFY(name##Changed),
        YUBIKEY_SIGNALS(SIGNAL_NAME_)
#undef  SIGNAL_NAME_
        "signalAccessKeyNotAccepted"
    };
    Q_STATIC_ASSERT(G_N_ELEMENTS(signalName) == SignalCount);
    if (iQueuedSignals) {
        // Reset first queued signal before emitting the signals.
        // Signal handlers may emit more signals.
        uint i = iFirstQueuedSignal;
        YubiKey* obj = parentObject();
        iFirstQueuedSignal = SignalCount;
        for (; i < SignalCount && iQueuedSignals; i++) {
            const SignalMask signalBit = (SignalMask(1) << i);
            if (iQueuedSignals & signalBit) {
                iQueuedSignals &= ~signalBit;
                HDEBUG(obj << signalName[i]);
                // See https://bugreports.qt.io/browse/QTBUG-18434
                QMetaObject::invokeMethod(obj, signalName[i]);
            }
        }
    }
}

inline
bool
YubiKey::Private::validTag()
{
    return iTag && iTag->yubiKeyId() == iYubiKeyId;
}

inline
bool
YubiKey::Private::canTransmit()
{
    return iTag && iTag->tagState() == YubiKeyTag::TagYubiKeyReady;
}

inline
bool
YubiKey::Private::haveAccess()
{
    switch (iAuthAccess) {
    case YubiKeyAuthAccessOpen:
    case YubiKeyAuthAccessGranted:
        return true;
    case YubiKeyAuthAccessUnknown:
    case YubiKeyAuthAccessDenied:
        break;
    }
    return false;
}

void
YubiKey::Private::verifyAuthorization()
{
    if (iPresent) {
        if (iTag->hasAuthChallenge()) {
            submitUnique(new AuthorizeOperation(parentObject(), false), true);
        } else {
            authorized(YubiKeyAuthAccessOpen);
        }
    }
}

void
YubiKey::Private::recheckAuthorization()
{
    if (iPresent) {
        submitUnique(new AuthorizeOperation(parentObject(), true), true);
    }
}

QByteArray
YubiKey::Private::nameToUtf8(
    const QString aName)
{
    QByteArray utf8(aName.toUtf8());

    if (utf8.size() > MaxNameLen) {
        QString name(aName);

        do {
            name.truncate(name.length() - 1);
            utf8 = name.toUtf8();
        } while (utf8.size() > MaxNameLen);
    }
    return utf8;
}

void
YubiKey::Private::staticAdapterEvent(
    NfcDefaultAdapter* aAdapter,
    NFC_DEFAULT_ADAPTER_PROPERTY,
    void* aSelf)
{
    Private* self = (Private*)aSelf;

    self->updatePath();
    self->emitQueuedSignals();
}

void
YubiKey::Private::onTotpTimer()
{
    updateTotpTimer();
    emitQueuedSignals();
}

void
YubiKey::Private::onTagStateChanged()
{
    updateTagState();
    emitQueuedSignals();
}

void
YubiKey::Private::onYubiKeyIdChanged()
{
    if (iTag &&
        iTag->tagState() == YubiKeyTag::TagYubiKeyReady &&
        iTag->yubiKeyId() != iYubiKeyId) {
        HDEBUG(qPrintable(iYubiKeyIdString) << "=>" <<
            qPrintable(YubiKeyUtil::toHex(iTag->yubiKeyId())) << "(reset?)");
        dropTag();
        updateTagState();
        emitQueuedSignals();
    }
}

void
YubiKey::Private::onYubiKeyVersionChanged()
{
    updateYubiKeyVersion();
    emitQueuedSignals();
}

void
YubiKey::Private::onYubiKeyAuthAlgorithmChanged()
{
    updateAuthAlgorithm();
    emitQueuedSignals();
}

void
YubiKey::Private::onYubiKeyAuthChallengeChanged()
{
    verifyAuthorization();
    emitQueuedSignals();
}

void
YubiKey::Private::onOperationIdsChanged()
{
    updateOperationIds();
    emitQueuedSignals();
}

void
YubiKey::Private::onAccessKeyChanged(
    const QByteArray aYubiKeyId,
    YubiKeyAlgorithm aAlgorithm)
{
    if (iYubiKeyId == aYubiKeyId) {
        HDEBUG(qPrintable(YubiKeyUtil::toHex(aYubiKeyId)) << aAlgorithm);
        updateAuthAccess(YubiKeyAuthAccessUnknown);
        verifyAuthorization();
        emitQueuedSignals();
    }
}

void
YubiKey::Private::dropTag()
{
    if (iCancel) {
        g_cancellable_cancel(iCancel);
        g_object_unref(iCancel);
        iCancel = Q_NULLPTR;
    }
    if (iTag) {
        iTag->disconnect(parentObject());
        iTag->disconnect(this);
        iTag->put();
        iTag = Q_NULLPTR;
    }
}

void
YubiKey::Private::updatePath()
{
    const char* activeTag = iAdapter->tags[0];

    if (activeTag) {
        const QString path(QString::fromLatin1(activeTag));

        if (!iTag || iTag->path() != path) {
            dropTag();
            iCancel = g_cancellable_new();
            iTag = YubiKeyTag::get(path);
            connect(iTag,
                SIGNAL(tagStateChanged()),
                SLOT(onTagStateChanged()));
            connect(iTag,
                SIGNAL(yubiKeyIdChanged()),
                SLOT(onYubiKeyIdChanged()));
            connect(iTag,
                SIGNAL(yubiKeyVersionChanged()),
                SLOT(onYubiKeyVersionChanged()));
            connect(iTag,
                SIGNAL(yubiKeyAuthAlgorithmChanged()),
                SLOT(onYubiKeyAuthAlgorithmChanged()));
            connect(iTag,
                SIGNAL(yubiKeyAuthChallengeChanged()),
                SLOT(onYubiKeyAuthChallengeChanged()));
            connect(iTag,
                SIGNAL(operationIdsChanged()),
                SLOT(onOperationIdsChanged()));
            parentObject()->connect(iTag,
                SIGNAL(operationFinished(int,bool)),
                SIGNAL(operationFinished(int,bool)));
            updateTagState();
        }
    } else if (iTag) {
        dropTag();
        updateTagState();
    }
}

void
YubiKey::Private::updateAuthAccess(
    YubiKeyAuthAccess aAccess)
{
    if (iAuthAccess != aAccess) {
        HDEBUG(iAuthAccess << "=>" << aAccess);
        iAuthAccess = aAccess;
        queueSignal(SignalAuthAccessChanged);
        if (!haveAccess()) {
            updateOtpList(QByteArray());
            updateOtpData(QByteArray());
            iTotpTimer->stop();
            if (iTotpValid) {
                iTotpValid = false;
                queueSignal(SignalTotpValidChanged);
            }
            if (iTotpTimeLeft) {
                iTotpTimeLeft = 0;
                queueSignal(SignalTotpTimeLeftChanged);
            }
        }
    }
}

void
YubiKey::Private::updateYubiKeyVersion()
{
    if (validTag()) {
        const QByteArray version(iTag->yubiKeyVersion());

        if (iYubiKeyVersion != version) {
            iYubiKeyVersion = version;
            iYubiKeyVersionString = YubiKeyUtil::versionToString(version);
            HDEBUG(qPrintable(iYubiKeyVersionString));
            queueSignal(SignalYubiKeyVersionChanged);
        }
    }
}

void
YubiKey::Private::updateAuthAlgorithm()
{
    if (validTag()) {
        const YubiKeyAlgorithm alg = iTag->yubiKeyAuthAlgorithm();

        if (iAuthAlgorithm != alg) {
            HDEBUG(iAuthAlgorithm << "=>" << alg);
            iAuthAlgorithm = alg;
        }
        verifyAuthorization();
    }
}

void
YubiKey::Private::updateOtpList(
    const QByteArray aOtpList)
{
    if (iOtpList != aOtpList) {
        iOtpList = aOtpList;
        iOtpListString = YubiKeyUtil::toHex(aOtpList);
        HDEBUG(qPrintable(iOtpListString));
        queueSignal(SignalOtpListChanged);
    }
}

void
YubiKey::Private::updateOtpData(
    const QByteArray aOtpData)
{
    if (iOtpData != aOtpData) {
        iOtpData = aOtpData;
        iOtpDataString = YubiKeyUtil::toHex(aOtpData);
        HDEBUG(qPrintable(iOtpDataString));
        queueSignal(SignalOtpDataChanged);
    }
}

void
YubiKey::Private::mergeOtpCode(
    const QByteArray aNameUtf8,
    const GUtilData* aRespData)
{
    uchar tag;
    GUtilRange otpData;
    GUtilData value;
    const guint8* startPtr = (guint8*)iOtpData.constData();
    const char* utf8 = aNameUtf8.constData();

    otpData.end = (otpData.ptr = startPtr) + iOtpData.size();
    while ((tag = YubiKeyUtil::readTLV(&otpData, &value)) != 0) {
        if (tag  == TLV_TAG_NAME &&
            aNameUtf8.size() == (int)value.size &&
            !memcmp(utf8, value.bytes, value.size)) {
            const gsize offset = otpData.ptr - startPtr;
            QByteArray newOtpData;

            // Replace the next TLV block
            switch (YubiKeyUtil::readTLV(&otpData, &value)) {
            case TLV_TAG_RESPONSE_FULL:
            case TLV_TAG_RESPONSE_TRUNCATED:
            case TLV_TAG_NO_RESPONSE:
            case TLV_TAG_RESPONSE_TOUCH:
                newOtpData.reserve(iOtpData.size() +
                    (((int)aRespData->size - (int)value.size)));
                // Leading part including the name TLV
                newOtpData.append((char*)startPtr, offset);
                // The new (presumably full) response data
                YubiKeyUtil::appendTLV(&newOtpData, TLV_TAG_RESPONSE_FULL,
                    (uchar) aRespData->size, aRespData->bytes);
                // And the remaining part (if any)
                newOtpData.append((char*)(value.bytes + value.size),
                    (int)(otpData.end - otpData.ptr));
                updateOtpData(newOtpData);
                break;
            default:
                HWARN("Unexpected response tag" << hex << tag);
                break;
            }
            return;
        }
    }
    HWARN(aNameUtf8.constData() << "OTP block not found");
}

void
YubiKey::Private::updateTagState()
{
    const bool present = (canTransmit() && iTag->yubiKeyId() == iYubiKeyId);

    if (iPresent != present) {
        iPresent = present;
        queueSignal(SignalPresentChanged);
        HDEBUG(qPrintable(iYubiKeyIdString) <<
            (present ? "present" : "not present"));
    }
    updateOperationIds();
    if (present) {
        updateAuthAlgorithm();
        updateYubiKeyVersion();
        verifyAuthorization();
    }
}

void
YubiKey::Private::updateOperationIds()
{
    const QList<int> ids((iTag && iTag->yubiKeyId() == iYubiKeyId) ?
        iTag->operationIds() : QList<int>());

    if (iOperationIds != ids) {
        iOperationIds = ids;
        HDEBUG(ids);
        queueSignal(SignalOperationIdsChanged);
    }
}

void
YubiKey::Private::authorized(
    YubiKeyAuthAccess aAccess)
{
    updateAuthAccess(aAccess);
    requestList();
}

void
YubiKey::Private::submit(
    YubiKeyTag::Operation* aOperation,  // Takes ownership
    bool aToFront)
{
    aOperation->submit(iTag, aToFront);
    aOperation->unref();
}

void
YubiKey::Private::submitUnique(
    YubiKeyTag::Operation* aOperation,  // Takes ownership
    bool aToFront)
{
    aOperation->submitUnique(iTag, aToFront);
    aOperation->unref();
}

inline
qint64
YubiKey::Private::currentPeriod()
{
    return QDateTime::currentMSecsSinceEpoch() / (TOTP_PERIOD_SEC * 1000);
}

void
YubiKey::Private::updateTotpTimer()
{
    const qint64 msecsSinceEpoch = QDateTime::currentMSecsSinceEpoch();
    const qint64 secsSinceEpoch = msecsSinceEpoch / 1000;
    const qint64 thisPeriod = secsSinceEpoch / TOTP_PERIOD_SEC;
    const int lastTotpTimeLeft = iTotpTimeLeft;

    if (iLastReceivedPeriod == thisPeriod) {
        const qint64 endOfThisPeriod = (thisPeriod + 1) * TOTP_PERIOD_SEC;
        const qint64 nextSecond = (secsSinceEpoch + 1) * 1000;

        iTotpTimeLeft = (int)(endOfThisPeriod - secsSinceEpoch);
        iTotpTimer->start((int)(nextSecond - msecsSinceEpoch));
        if (!iTotpValid) {
            iTotpValid = true;
            queueSignal(SignalTotpValidChanged);
        }
    } else {
        iTotpTimeLeft = 0;
        iTotpTimer->stop();
        if (iTotpValid) {
            iTotpValid = false;
            queueSignal(SignalTotpValidChanged);
        }
        // Try to refresh the passwords
        submitCalculateAll();
    }

    if (lastTotpTimeLeft != iTotpTimeLeft) {
        queueSignal(SignalTotpTimeLeftChanged);
    }
}

void
YubiKey::Private::requestList(
    bool aRightAway)
{
    submitUnique(new ListOperation(parentObject()), aRightAway);
}

const NfcIsoDepApdu*
YubiKey::Private::buildCalculateAllApdu(
    CalculateAllApdu* aApdu)
{
    const quint64 challenge = htobe64(iLastRequestedPeriod = currentPeriod());
    NfcIsoDepApdu* apdu = &aApdu->iApdu;

    // Calculate All Data
    //
    // +------------------+---------------------+
    // | Challenge tag    | 0x74                |
    // | Challenge length | Length of challenge |
    // | Challenge data   | Challenge           |
    // +------------------+---------------------+
    aApdu->iData[0] = TLV_TAG_CHALLENGE;
    aApdu->iData[1] = sizeof(challenge);
    memcpy(aApdu->iData + 2, &challenge, sizeof(challenge));

    *apdu = CMD_CALCULATE_ALL_TEMPLATE;
    apdu->data.bytes = aApdu->iData;
    apdu->data.size = sizeof(aApdu->iData);
    return apdu;
}

void
YubiKey::Private::submitCalculateAll()
{
    if (canTransmit()) {
        CalculateAllApdu apdu;

        if (iTag->transmit(buildCalculateAllApdu(&apdu), this,
            G_STRINGIFY(CALCULATE_ALL_RESP_METHOD), iCancel)) {
            HDEBUG("CALCULATE_ALL");
        } else {
            HDEBUG("CALCULATE_ALL - oops!");
        }
    }
}

void
YubiKey::Private::calculateAllOk(
    const QByteArray aData)
{
    iLastReceivedPeriod = iLastRequestedPeriod;
    updateOtpData(aData);
    updateTotpTimer();
}

void
YubiKey::Private::CALCULATE_ALL_RESP_METHOD(
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    if (!aError && aSw == RC_OK) {
        HDEBUG("CALCULATE_ALL ok" << aResp->size <<
            qPrintable(YubiKeyUtil::toHex(aResp)));
        if (haveAccess()) {
            calculateAllOk(YubiKeyUtil::toByteArray(aResp));
            emitQueuedSignals();
        } else {
            HDEBUG("CALCULATE_ALL ignored");
        }
    } else {
        REPORT_ERROR("CALCULATE_ALL", aSw, aError);
    }
}

int
YubiKey::Private::reset()
{
    if (canTransmit()) {
        const int id = iTag->transmit(&CMD_RESET, this,
            G_STRINGIFY(RESET_RESP_METHOD), iCancel);

        if (id) {
            HDEBUG("RESET");
            return id;
        } else {
            HDEBUG("RESET - oops!");
        }
    }
    return 0;
}

void
YubiKey::Private::RESET_RESP_METHOD(
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    if (!aError && aSw == RC_OK) {
        HDEBUG("RESET ok" << qPrintable(YubiKeyUtil::toHex(aResp)));
        // The data associated with the old YubiKey ID are no longer valid
        YubiKeyUtil::configDir(iYubiKeyId).removeRecursively();
        // Successful RESET changes the ID, need to re-SELECT the app
        recheckAuthorization();
        emitQueuedSignals();
        Q_EMIT parentObject()->yubiKeyReset();
    } else {
        REPORT_ERROR("RESET", aSw, aError);
    }
}

bool
YubiKey::Private::submitPassword(
    const QString aPassword,
    bool aSave)
{
    if (iAuthAlgorithm != YubiKeyAlgorithm_Unknown) {
        // Temporary disconnect the signal to avoid double authorization check
        disconnect(iAuth,
            SIGNAL(accessKeyChanged(QByteArray,YubiKeyAlgorithm)), this,
            SLOT(onAccessKeyChanged(QByteArray,YubiKeyAlgorithm)));
        if (iAuth->setPassword(iYubiKeyId, iAuthAlgorithm, aPassword, aSave)) {
            updateAuthAccess(YubiKeyAuthAccessUnknown);
        }
        // Force re-check even if password didn't change
        recheckAuthorization();
        // Re-connect the signal
        connect(iAuth,
            SIGNAL(accessKeyChanged(QByteArray,YubiKeyAlgorithm)), this,
            SLOT(onAccessKeyChanged(QByteArray,YubiKeyAlgorithm)));
        return true;
    } else {
        return false;
    }
}

int
YubiKey::Private::setPassword(
    const QString aPassword)
{
    int id = 0;

    //
    // https://developers.yubico.com/OATH/YKOATH_Protocol.html
    //
    // SET CODE INSTRUCTION
    //
    // Configures Authentication. If length 0 is sent, authentication is
    // removed. The key to be set is expected to be a user-supplied UTF-8
    // encoded password passed through 1000 rounds of PBKDF2 with the ID
    // from select used as salt. 16 bytes of that are used. When configuring
    // authentication you are required to send an 8 byte challenge and one
    // authentication-response with that key, in order to confirm that the
    // application and the host software can calculate the same response
    // for that key.
    //
    if (!canTransmit()) {
        HDEBUG("Can't change the password");
    } else if (!aPassword.isEmpty()) {
        YubiKeyAlgorithm alg = iTag->yubiKeyAuthAlgorithm();
        if (alg == YubiKeyAlgorithm_Unknown) {
            alg = YubiKeyAlgorithm_Default;
        }
        const QByteArray key(YubiKeyAuth::calculateAccessKey(iYubiKeyId, alg, aPassword));
        const QByteArray challenge(YubiKeyUtil::toByteArray(foil_random_bytes(CHALLENGE_LEN)));
        const QByteArray response(YubiKeyAuth::calculateResponse(key, challenge, alg));

        HDEBUG("Host challenge:" << qPrintable(YubiKeyUtil::toHex(challenge)));
        HDEBUG("Response:" << qPrintable(YubiKeyUtil::toHex(response)));

        // Set Code Data
        //
        // +------------------+---------------------+
        // | Key tag          | 0x73                |
        // | Key length       | Length of key + 1   |
        // | Key algorithm    | Algorithm           |
        // | Key data         | Key                 |
        // +------------------+---------------------+
        // | Challenge tag    | 0x74                |
        // | Challenge length | Length of challenge |
        // | Challenge data   | Challenge           |
        // +------------------+---------------------+
        // | Response tag     | 0x75                |
        // | Response length  | Length of response  |
        // | Response data    | Response            |
        // +------------------+---------------------+
        NfcIsoDepApdu cmd = CMD_SET_CODE;
        QByteArray data;

        data.reserve(key.size() + CHALLENGE_LEN + response.size() + 7);
        data.append((char)TLV_TAG_KEY);
        data.append((char)(key.size() + 1));
        data.append((char)YubiKeyUtil::algorithmValue(alg));
        data.append(key);
        YubiKeyUtil::appendTLV(&data, TLV_TAG_CHALLENGE, challenge);
        YubiKeyUtil::appendTLV(&data, TLV_TAG_RESPONSE_FULL, response);

        cmd.data.bytes = (const guint8*)data.constData();
        cmd.data.size = data.size();
        id = iTag->transmit(&cmd, this,
            G_STRINGIFY(SET_CODE_RESP_METHOD), iCancel);
        DUMP_CMD("SET_CODE", cmd, id);
    } else {
        id = iTag->transmit(&CMD_SET_CODE, this,
            G_STRINGIFY(REMOVE_CODE_RESP_METHOD), iCancel);
        DUMP_CMD("SET_CODE", CMD_SET_CODE, id);
    }
    return id;
}

void
YubiKey::Private::SET_CODE_RESP_METHOD(
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    setCodeResp(aResp, aSw, aError, &YubiKey::passwordChanged);
}

void
YubiKey::Private::REMOVE_CODE_RESP_METHOD(
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    setCodeResp(aResp, aSw, aError, &YubiKey::passwordRemoved);
}

void
YubiKey::Private::setCodeResp(
    const GUtilData* aResp,
    guint aSw,
    const GError* aError,
    SignalEmitter aSignal)
{
    if (!aError && aSw == RC_OK) {
        HDEBUG("SET_CODE ok" << qPrintable(YubiKeyUtil::toHex(aResp)));
        iAuth->clearPassword(iYubiKeyId);
        recheckAuthorization();
        Q_EMIT (parentObject()->*(aSignal))();
    } else {
        REPORT_ERROR("SET_CODE", aSw, aError);
    }
}

int
YubiKey::Private::submitPutHotpToken(
    YubiKeyAlgorithm aAlgorithm,
    const QString aName,
    const QByteArray aSecret,
    int aDigits,
    int aCounter)
{
    int id = 0;

    if (!canTransmit()) {
        HDEBUG("Can't send TOTP token");
    } else {
        // Put Data
        //
        // +-----------------+----------------------------------------------+
        // | Name tag        | 0x71                                         |
        // | Name length     | Length of name data, max 64 bytes            |
        // | Name data       | Name                                         |
        // +-----------------+----------------------------------------------+
        // | Key tag         | 0x73                                         |
        // | Key length      | Length of key data + 2                       |
        // | Key algorithm   | High 4 bits is type, low 4 bits is algorithm |
        // | Digits          | Number of digits in OATH code                |
        // | Key data        | Key                                          |
        // +-----------------+----------------------------------------------+
        // | Property tag(o) | 0x78                                         |
        // | Property(o)     | Property byte                                |
        // +-----------------+----------------------------------------------+
        // | IMF tag(o)      | 0x7a (only valid for HOTP)                   |
        // | IMF length(o)   | Length of imf data, always 4 bytes           |
        // | IMF data(o)     | Imf                                          |
        // +-----------------+----------------------------------------------+
        const QByteArray nameUtf8(nameToUtf8(aName));

        // YubiKey seems to require at least 14 bytes of key data
        // Pad the key with zeros if necessary
        const int keySize = qMax(aSecret.size(), (int) MinKeySize);
        const int padding = keySize - aSecret.size();
        const guint32 imf = htobe32(aCounter);

        NfcIsoDepApdu cmd = CMD_PUT_TEMPLATE;
        QByteArray data;

        data.reserve(nameUtf8.size() + keySize + 16);
        YubiKeyUtil::appendTLV(&data, TLV_TAG_NAME, nameUtf8);
        data.append((char)TLV_TAG_KEY);
        data.append((char)(keySize + 2));
        data.append((char)(TYPE_HOTP | YubiKeyUtil::algorithmValue(aAlgorithm)));
        data.append((char)aDigits);
        data.append(aSecret);
        for (int i = 0; i < padding; i++) {
            data.append((char)0);
        }
        data.append((char)TLV_TAG_PROPERTY);
        data.append((char)PROP_REQUIRE_TOUCH);
        YubiKeyUtil::appendTLV(&data, TLV_TAG_IMF, sizeof(imf), &imf);

        cmd.data.bytes = (const guint8*)data.constData();
        cmd.data.size = data.size();
        id = iTag->transmit(&cmd, this, G_STRINGIFY(PUT_RESP_METHOD), iCancel);
        DUMP_CMD("PUT", cmd, id);
    }
    return id;
}

int
YubiKey::Private::submitPutTotpToken(
    YubiKeyAlgorithm aAlgorithm,
    const QString aName,
    const QByteArray aSecret,
    int aDigits)
{
    int id = 0;

    if (!canTransmit()) {
        HDEBUG("Can't send TOTP token");
    } else {
        // Put Data
        //
        // +-----------------+----------------------------------------------+
        // | Name tag        | 0x71                                         |
        // | Name length     | Length of name data, max 64 bytes            |
        // | Name data       | Name                                         |
        // +-----------------+----------------------------------------------+
        // | Key tag         | 0x73                                         |
        // | Key length      | Length of key data + 2                       |
        // | Key algorithm   | High 4 bits is type, low 4 bits is algorithm |
        // | Digits          | Number of digits in OATH code                |
        // | Key data        | Key                                          |
        // +-----------------+----------------------------------------------+
        // | Property tag(o) | 0x78                                         |
        // | Property(o)     | Property byte                                |
        // +-----------------+----------------------------------------------+

        // YubiKey seems to require at least 14 bytes of key data
        // Pad the key with zeros if necessary
        const int keySize = qMax(aSecret.size(), (int) MinKeySize);
        const int padding = keySize - aSecret.size();

        const QByteArray nameUtf8(nameToUtf8(aName));
        NfcIsoDepApdu cmd = CMD_PUT_TEMPLATE;
        QByteArray data;

        data.reserve(nameUtf8.size() + keySize + 8);
        YubiKeyUtil::appendTLV(&data, TLV_TAG_NAME, nameUtf8);
        data.append((char)TLV_TAG_KEY);
        data.append((char)(keySize + 2));
        data.append((char)(TYPE_TOTP | YubiKeyUtil::algorithmValue(aAlgorithm)));
        data.append((char)aDigits);
        data.append(aSecret);
        for (int i = 0; i < padding; i++) {
            data.append((char)0);
        }

        cmd.data.bytes = (const guint8*)data.constData();
        cmd.data.size = data.size();
        id = iTag->transmit(&cmd, this, G_STRINGIFY(PUT_RESP_METHOD), iCancel);
        DUMP_CMD("PUT", cmd, id);
    }
    return id;
}

void
YubiKey::Private::PUT_RESP_METHOD(
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    if (!aError && aSw == RC_OK) {
        HDEBUG("PUT ok" << qPrintable(YubiKeyUtil::toHex(aResp)));
        requestList();
        emitQueuedSignals();
    } else {
        REPORT_ERROR("PUT", aSw, aError);
    }
}

// ==========================================================================
// YubiKey::Private::AuthorizeOperation
// ==========================================================================

YubiKey::Private::AuthorizeOperation::AuthorizeOperation(
    YubiKey* aKey,
    bool aRequireSelect) :
    KeyOperation("Authorize", aKey, aRequireSelect),
    iAlgorithm(YubiKeyAlgorithm_Unknown)
{
}

bool
YubiKey::Private::AuthorizeOperation::startOperation()
{
    //
    // https://developers.yubico.com/OATH/YKOATH_Protocol.html
    //
    // VALIDATE INSTRUCTION
    //
    // Validates authentication (mutually). The challenge for this comes
    // from the SELECT command. The response if computed by performing
    // the correct HMAC function of that challenge with the correct key.
    // A new challenge is then sent to the application, together with
    // the response. The application will then respond with a similar
    // calculation that the host software can verify.
    //
    YubiKey::Private* priv = iYubiKey->iPrivate;
    YubiKeyTag* tag = priv->iTag;
    const QByteArray challenge(tag->yubiKeyAuthChallenge());
    bool started = true;

    // Successful RESET cancels this operation, IDs must match
    HASSERT(priv->iYubiKeyId == tag->yubiKeyId());

    if (challenge.isEmpty()) {
        priv->authorized(YubiKeyAuthAccessOpen);
        finished();
    } else {
        iAlgorithm = tag->yubiKeyAuthAlgorithm();
        iAccessKey = priv->iAuth->getAccessKey(priv->iYubiKeyId, iAlgorithm);
        if (iAccessKey.isEmpty()) {
            priv->updateAuthAccess(YubiKeyAuthAccessDenied);
            HDEBUG("No" << iAlgorithm << "access key for" <<
                qPrintable(YubiKeyUtil::toHex(priv->iYubiKeyId)));
            finished();
        } else {
            const QByteArray response(YubiKeyAuth::calculateResponse(iAccessKey,
                challenge, iAlgorithm));

            iHostChallenge = YubiKeyUtil::toByteArray(foil_random_bytes(CHALLENGE_LEN));
            HDEBUG("Response:" << qPrintable(YubiKeyUtil::toHex(response)));
            HDEBUG("Host challenge:" << qPrintable(YubiKeyUtil::toHex(iHostChallenge)));

            // Validate Data
            //
            // +------------------+---------------------+
            // | Response tag     | 0x75                |
            // | Response length  | Length of response  |
            // | Response data    | Response            |
            // +------------------+---------------------+
            // | Challenge tag    | 0x74                |
            // | Challenge length | Length of challenge |
            // | Challenge data   | Challenge           |
            // +------------------+---------------------+
            NfcIsoDepApdu validateCmd = CMD_VALIDATE_TEMPLATE;
            QByteArray validateData;

            validateData.reserve(ACCESS_KEY_LEN + CHALLENGE_LEN + 4);
            YubiKeyUtil::appendTLV(&validateData, TLV_TAG_RESPONSE_FULL, response);
            YubiKeyUtil::appendTLV(&validateData, TLV_TAG_CHALLENGE, iHostChallenge);

            validateCmd.data.bytes = (guint8*)validateData.constData();
            validateCmd.data.size = validateData.size();
            started = transmit(&validateCmd, validateResp);
#if HARBOUR_DEBUG
            if (started) {
                HDEBUG("VALIDATE" << qPrintable(YubiKeyUtil::toHex(&validateCmd.data)));
            }
#endif // HARBOUR_DEBUG
        }
    }

    priv->emitQueuedSignals();
    return started;
}

void
YubiKey::Private::AuthorizeOperation::validateResp(
    Operation* aSelf,
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    AuthorizeOperation* self = (AuthorizeOperation*)aSelf;
    YubiKey* key = self->iYubiKey;
    YubiKey::Private* priv = key->iPrivate;

    // Response Codes
    //
    // +------------------+--------+
    // | Success          | 0x9000 |
    // +------------------+--------+
    // | Auth not enabled | 0x6984 |
    // +------------------+--------+
    // | Wrong syntax     | 0x6a80 |
    // +------------------+--------+
    // | Generic error    | 0x6581 |
    // +------------------+--------+
    if (!aError && aResp) {
        bool authorized = false;

        HDEBUG("VALIDATE" << hex << aSw << qPrintable(YubiKeyUtil::toHex(aResp)));
        if (aSw == RC_OK || aSw == RC_AUTH_NOT_ENABLED) {
            QByteArray cardResp;
            GUtilRange resp;
            GUtilData data;
            uchar tag;

            resp.end = (resp.ptr = aResp->bytes) + aResp->size;
            while ((tag = YubiKeyUtil::readTLV(&resp, &data)) != 0) {
                if (tag == TLV_TAG_RESPONSE_FULL) {
                    cardResp = QByteArray((char*)data.bytes, data.size);
                    HDEBUG("Card response:" << qPrintable(YubiKeyUtil::toHex(cardResp)));
                    break;
                }
            }

            // Validate the response sent by the app
            if (!cardResp.isEmpty()) {
                const QByteArray hostResp(YubiKeyAuth::calculateResponse
                    (self->iAccessKey, self->iHostChallenge, self->iAlgorithm));
                HDEBUG("Host response:" << qPrintable(YubiKeyUtil::toHex(hostResp)));
                if (hostResp == cardResp) {
                    HDEBUG("Match!");
                    authorized = true;
                }
            }
        }

        if (authorized) {
            priv->authorized(YubiKeyAuthAccessGranted);
        } else {
            priv->updateAuthAccess(YubiKeyAuthAccessDenied);
            if (aSw == RC_WRONG_SYNTAX) {
                HDEBUG("Invalid password?");
                priv->queueSignal(SignalAccessKeyNotAccepted);
            }
        }
    } else {
        REPORT_ERROR("VALIDATE", aSw, aError);
    }
    priv->emitQueuedSignals();
    self->finished(!aError);
}

// ==========================================================================
// YubiKey::Private::KeyOperation
// ==========================================================================

YubiKey::Private::KeyOperation::KeyOperation(
    const char* aName,
    YubiKey* aKey,
    bool aRequireSelect) :
    Operation(aName, aKey->iPrivate->iCancel, aRequireSelect),
    iYubiKey(aKey->ref())
{
}

YubiKey::Private::KeyOperation::~KeyOperation()
{
    iYubiKey->unref();
}

// ==========================================================================
// YubiKey::Private::ListOperation
// ==========================================================================

YubiKey::Private::ListOperation::ListOperation(
    YubiKey* aKey) :
    KeyOperation("List", aKey)
{
}

bool
YubiKey::Private::ListOperation::startOperation()
{
    HDEBUG("LIST");
    return transmit(&CMD_LIST, listResp);
}

void
YubiKey::Private::ListOperation::listResp(
    Operation* aSelf,
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    ListOperation* self = (ListOperation*)aSelf;
    YubiKey* key = self->iYubiKey;
    YubiKey::Private* priv = key->iPrivate;

    if (RC_MORE_DATA(aSw)) {
        HDEBUG("LIST (partial)" << aResp->size << "bytes");
        self->iRespBuf.append((const char*)aResp->bytes, aResp->size);
        HDEBUG("CMD_SEND_REMAINING" << self->iRespBuf.size() << "+" <<
            (NFC_ISODEP_SW2(aSw) ? NFC_ISODEP_SW2(aSw) : 0x100));
        if (self->transmit(&CMD_SEND_REMAINING, listResp)) {
            // More LIST data is coming our way
            priv->emitQueuedSignals();
            return;
        }
    } else if (!aError && aResp && aSw == RC_OK) {
        HDEBUG("LIST" << aResp->size << "bytes");
        self->iRespBuf.append((const char*)aResp->bytes, aResp->size);
        HDEBUG("LIST ok" << qPrintable(YubiKeyUtil::toHex(self->iRespBuf)));
        priv->updateOtpList(self->iRespBuf);
        if (!priv->iOtpListFetched) {
            priv->iOtpListFetched = true;
            priv->queueSignal(SignalOtpListFetchedChanged);
        }

        // Don't request auth data if the list is empty
        if (self->iRespBuf.isEmpty()) {
            priv->updateOtpData(self->iRespBuf);
        } else {
            CalculateAllApdu apdu;

            HDEBUG("CALCULATE_ALL");
            self->iRespBuf.truncate(0);
            if (self->transmit(priv->buildCalculateAllApdu(&apdu), calculateAllResp)) {
                // Not finished yet
                priv->emitQueuedSignals();
                return;
            }
        }
        priv->emitQueuedSignals();
    } else {
        REPORT_ERROR("LIST", aSw, aError);
    }
    self->finished(!aError);
}

void
YubiKey::Private::ListOperation::calculateAllResp(
    Operation* aOperation,
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    ListOperation* self = (ListOperation*)aOperation;
    YubiKey* key = self->iYubiKey;
    YubiKey::Private* priv = key->iPrivate;

    if (RC_MORE_DATA(aSw)) {
        HDEBUG("CALCULATE_ALL (partial)" << aResp->size << "bytes");
        self->iRespBuf.append((const char*)aResp->bytes, aResp->size);
        HDEBUG("CMD_SEND_REMAINING" << self->iRespBuf.size() << "+" <<
            (NFC_ISODEP_SW2(aSw) ? NFC_ISODEP_SW2(aSw) : 0x100));
        if (self->transmit(&CMD_SEND_REMAINING, calculateAllResp)) {
            // More CALCULATE_ALL data is coming our way
            priv->emitQueuedSignals();
            return;
        }
    } else if (!aError && aSw == RC_OK) {
        HDEBUG("CALCULATE_ALL" << aResp->size << "bytes");
        self->iRespBuf.append((const char*)aResp->bytes, aResp->size);
        HDEBUG("CALCULATE_ALL ok" << qPrintable(YubiKeyUtil::toHex(self->iRespBuf)));
        priv->calculateAllOk(self->iRespBuf);
        priv->emitQueuedSignals();
    } else {
        REPORT_ERROR("CALCULATE_ALL", aSw, aError);
    }
    self->finished(!aError);
}

// ==========================================================================
// YubiKey::Private::RefreshOperation
// ==========================================================================

YubiKey::Private::RefreshOperation::RefreshOperation(
    YubiKey* aKey,
    const QStringList aNames) :
    KeyOperation("Refresh", aKey),
    iNames(aNames),
    iNextName(0)
{
}

bool
YubiKey::Private::RefreshOperation::startOperation()
{
    return calculateNext();
}

bool
YubiKey::Private::RefreshOperation::calculateNext()
{
    if (iNextName < iNames.count()) {
        YubiKey::Private* priv = iYubiKey->iPrivate;
        const quint64 challenge = htobe64(priv->iLastRequestedPeriod);

        // Calculate Data
        //
        // +------------------+---------------------+
        // | Name tag         | 0x71                |
        // | Name length      | Length of name data |
        // | Name data        | Name                |
        // +------------------+---------------------+
        // | Challenge tag    | 0x74                |
        // | Challenge length | Length of challenge |
        // | Challenge data   | Challenge           |
        // +------------------+---------------------+
        NfcIsoDepApdu calculateCmd = CMD_CALCULATE_TEMPLATE;
        QByteArray calculateData;

        iLastNameUtf8 = nameToUtf8(iNames.at(iNextName));
        calculateData.reserve(iLastNameUtf8.size() + sizeof(challenge) + 4);
        YubiKeyUtil::appendTLV(&calculateData, TLV_TAG_NAME, iLastNameUtf8);
        YubiKeyUtil::appendTLV(&calculateData, TLV_TAG_CHALLENGE,
            sizeof(challenge), &challenge);

        calculateCmd.data.bytes = (guint8*)calculateData.constData();
        calculateCmd.data.size = calculateData.size();
        HDEBUG("CALCULATE" << iNextName << iNames.at(iNextName));
        if (transmit(&calculateCmd, calculateResp)) {
            iNextName++;
            return true;
        }
    }
    return false;
}

void
YubiKey::Private::RefreshOperation::calculateResp(
    Operation* aOperation,
    const GUtilData* aResp,
    guint aSw,
    const GError* aError)
{
    RefreshOperation* self = (RefreshOperation*)aOperation;
    YubiKey* key = self->iYubiKey;
    YubiKey::Private* priv = key->iPrivate;

    if (!aError) {
        if (aSw == RC_OK) {
            GUtilRange resp;
            GUtilData data;

            HDEBUG("CALCULATE" << self->iLastNameUtf8.constData() << "ok");
            resp.end = (resp.ptr = aResp->bytes) + aResp->size;
            if (YubiKeyUtil::readTLV(&resp, &data) == TLV_TAG_RESPONSE_FULL) {
                HDEBUG("Response:" << qPrintable(YubiKeyUtil::toHex(&data)));
                priv->mergeOtpCode(self->iLastNameUtf8, &data);
            }
        } else{
            HDEBUG("CALCULATE error" << hex << aSw);
        }
        // Try the next one anyway
        if (self->calculateNext()) {
            // Not finished yet
            return;
        }
    } else {
        REPORT_ERROR("CALCULATE", aSw, aError);
    }
    self->finished(!aError);
}

// ==========================================================================
// YubiKey::Private::DeleteOperation
// ==========================================================================

YubiKey::Private::DeleteOperation::DeleteOperation(
    YubiKey* aKey,
    const QStringList aNames) :
    KeyOperation("Delete", aKey),
    iNames(aNames),
    iNextName(0)
{
}

bool
YubiKey::Private::DeleteOperation::startOperation()
{
    return deleteNext();
}

bool
YubiKey::Private::DeleteOperation::deleteNext()
{
    if (iNextName < iNames.count()) {
        // Delete Data
        //
        // +-------------+---------------------+
        // | Name tag    | 0x71                |
        // | Name length | Length of name data |
        // | Name data   | Name                |
        // +-------------+---------------------+
        const QByteArray nameUtf8(nameToUtf8(iNames.at(iNextName)));
        NfcIsoDepApdu deleteCmd = CMD_DELETE_TEMPLATE;
        QByteArray deleteData;

        deleteData.reserve(nameUtf8.size() + 2);
        YubiKeyUtil::appendTLV(&deleteData, TLV_TAG_NAME, nameUtf8);

        deleteCmd.data.bytes = (guint8*)deleteData.constData();
        deleteCmd.data.size = deleteData.size();
        HDEBUG("DELETE" << iNextName << iNames.at(iNextName));
        if (transmit(&deleteCmd, deleteResp)) {
            iNextName++;
            return true;
        }
    }
    return false;
}

void
YubiKey::Private::DeleteOperation::deleteResp(
    Operation* aOperation,
    const GUtilData*,
    guint aSw,
    const GError* aError)
{
    DeleteOperation* self = (DeleteOperation*)aOperation;
    YubiKey* key = self->iYubiKey;
    YubiKey::Private* priv = key->iPrivate;

    if (!aError) {
#if HARBOUR_DEBUG
        if (aSw == RC_OK) {
            HDEBUG("DELETE ok");
        } else{
            HDEBUG("DELETE error" << hex << aSw);
        }
#endif // HARBOUR_DEBUG
        // Try the next one anyway
        if (self->deleteNext()) {
            // Not finished yet
            return;
        } else {
            priv->requestList(true);
        }
    } else {
        REPORT_ERROR("DELETE", aSw, aError);
    }
    self->finished(!aError);
}

// ==========================================================================
// YubiKey
// ==========================================================================

YubiKey::YubiKey(
    const QByteArray aYubiKeyId) :
    iPrivate(new Private(aYubiKeyId, this))
{
    HDEBUG(qPrintable(iPrivate->iYubiKeyIdString));

    // Add it to the static map
    HASSERT(!Private::gMap.value(aYubiKeyId));
    Private::gMap.insert(aYubiKeyId, this);

    // Update the path once the object has been fully set up
    iPrivate->updatePath();
    iPrivate->iFirstQueuedSignal = Private::SignalCount; // Clear queued events
}

YubiKey::~YubiKey()
{
    HDEBUG(qPrintable(iPrivate->iYubiKeyIdString));

    // Remove it from the static map
    HASSERT(Private::gMap.value(iPrivate->iYubiKeyId) == this);
    Private::gMap.remove(iPrivate->iYubiKeyId);

    delete iPrivate;
}

YubiKey*
YubiKey::get(
    const QByteArray aYubiKeyId)
{
    YubiKey* key = Private::gMap.value(aYubiKeyId);
    if (key) {
        return key->ref();
    } else {
        return new YubiKey(aYubiKeyId);
    }
}

YubiKey*
YubiKey::ref()
{
    iPrivate->iRef.ref();
    return this;
}

void
YubiKey::unref()
{
    if (!iPrivate->iRef.deref()) {
        delete this;
    }
}

bool
YubiKey::present() const
{
    return iPrivate->iPresent;
}

YubiKeyAuthAccess
YubiKey::authAccess() const
{
    return iPrivate->iAuthAccess;
}

const QByteArray
YubiKey::yubiKeyId() const
{
    return iPrivate->iYubiKeyId;
}

const QByteArray
YubiKey::yubiKeyVersion() const
{
    return iPrivate->iYubiKeyVersion;
}

const QString
YubiKey::yubiKeyIdString() const
{
    return iPrivate->iYubiKeyIdString;
}

const QString
YubiKey::yubiKeyVersionString() const
{
    return iPrivate->iYubiKeyVersionString;
}

const QByteArray
YubiKey::otpList() const
{
    return iPrivate->iOtpList;
}

const QString
YubiKey::otpListString() const
{
    return iPrivate->iOtpListString;
}

const QByteArray
YubiKey::otpData() const
{
    return iPrivate->iOtpData;
}

const QString
YubiKey::otpDataString() const
{
    return iPrivate->iOtpDataString;
}

bool
YubiKey::otpListFetched() const
{
    return iPrivate->iOtpListFetched;
}

const QList<int>
YubiKey::operationIds() const
{
    return iPrivate->iOperationIds;
}

bool
YubiKey::totpValid() const
{
    return iPrivate->iTotpValid;
}

int
YubiKey::totpTimeLeft() const
{
    return iPrivate->iTotpTimeLeft;
}

int
YubiKey::reset()
{
    return iPrivate->reset();
}

void
YubiKey::refreshTokens(
    const QStringList aTokens)
{
    if (!aTokens.isEmpty() && iPrivate->canTransmit()) {
        HDEBUG(aTokens);
        iPrivate->submit(new Private::RefreshOperation(this, aTokens));
    }
}

void
YubiKey::deleteTokens(
    const QStringList aTokens)
{
    if (!aTokens.isEmpty() && iPrivate->canTransmit()) {
        HDEBUG(aTokens);
        iPrivate->submit(new Private::DeleteOperation(this, aTokens));
    }
}

int
YubiKey::putHotpToken(
    YubiKeyAlgorithm aAlgorithm,
    const QString aName,
    const QByteArray aSecret,
    int aDigits,
    int aCounter)
{
    if (aDigits >= YubiKeyUtil::MinDigits &&
        aDigits <= YubiKeyUtil::MaxDigits) {
        return iPrivate->submitPutHotpToken(aAlgorithm, aName, aSecret,
            aDigits, aCounter);
    }
    return false;
}

int
YubiKey::putTotpToken(
    YubiKeyAlgorithm aAlgorithm,
    const QString aName,
    const QByteArray aSecret,
    int aDigits)
{
    if (aDigits >= YubiKeyUtil::MinDigits &&
        aDigits <= YubiKeyUtil::MaxDigits) {
        return iPrivate->submitPutTotpToken(aAlgorithm, aName, aSecret,
            aDigits);
    }
    return false;
}

bool
YubiKey::submitPassword(
    const QString aPassword,
    bool aSave)
{
    return iPrivate->submitPassword(aPassword, aSave);
}

int
YubiKey::setPassword(
    const QString aPassword)
{
    return iPrivate->setPassword(aPassword);
}

#include "YubiKey.moc"
