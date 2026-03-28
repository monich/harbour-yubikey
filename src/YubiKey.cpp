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

#include "YubiKey.h"

#include "YubiKeyAuth.h"
#include "YubiKeyIo.h"
#include "YubiKeyOpQueue.h"
#include "YubiKeyUtil.h"

#include "HarbourBase32.h"
#include "HarbourDebug.h"
#include "HarbourParentSignalQueueObject.h"
#include "HarbourUtil.h"

#include <QtCore/QDateTime>
#include <QtCore/QListIterator>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QtEndian>

// s(SignalName,signalName)
#define QUEUED_SIGNALS(s) \
    s(YubiKeyIo,yubiKeyIo) \
    s(Present,present) \
    s(OtpList,otpList) \
    s(OtpListFetched,otpListFetched) \
    s(UpdatingPasswords,updatingPasswords) \
    s(HaveTotpCodes,haveTotpCodes) \
    s(HaveBeenReset,haveBeenReset) \
    s(TotpTimeLeft,totpTimeLeft)

// ==========================================================================
// YubiKey::Private
// ==========================================================================

enum YubiKeySignal {
    #define SIGNAL_ENUM_(Name,name) Signal##Name##Changed,
    QUEUED_SIGNALS(SIGNAL_ENUM_)
    #undef  SIGNAL_ENUM_
    YubiKeySignalCount
};

typedef HarbourParentSignalQueueObject<YubiKey,
    YubiKeySignal, YubiKeySignalCount>
    YubiKeyPrivateBase;

class YubiKey::Private :
    public YubiKeyPrivateBase,
    public YubiKeyConstants
{
    Q_OBJECT
    static const SignalEmitter gSignalEmitters[];
    static const YubiKeyIo::APDU LIST_APDU;
    static const YubiKeyIo::APDU CALCULATE_ALL_APDU;

    // (relatively) easy way to get command specific OpData from a YubiKeyOp
    // completion slot
    template <typename T> T* senderOpData() const
        { return static_cast<T*>(qobject_cast<YubiKeyOp*>(sender())->opData()); }

public:
    typedef QList<YubiKeyOtp> OtpList;
    typedef QListIterator<YubiKeyOtp> OtpListIterator;
    typedef QMutableListIterator<YubiKeyOtp> OtpMutableListIterator;
    struct OtpListData : public YubiKeyOp::OpData
    {   // Pass the LIST output to the CALCULATE_ALL completion slot
        const OtpList iOtpList;
        OtpListData(const OtpList& aList) : iOtpList(aList) {}
        ~OtpListData() Q_DECL_OVERRIDE {}
    };
    struct StringData : public YubiKeyOp::OpData
    {
        const QString iString;
        StringData(const QString& aString) : iString(aString) {}
        ~StringData() Q_DECL_OVERRIDE {}
    };
    struct BytesData : public YubiKeyOp::OpData
    {
        const QByteArray iBytes;
        BytesData(const QByteArray& aBytes) : iBytes(aBytes) {}
        ~BytesData() Q_DECL_OVERRIDE {}
    };

    enum {
        MinKeySize = 14
    };

    Private(YubiKey*);

    static YubiKeyTokenType toAuthType(uchar);
    static YubiKeyAlgorithm toAuthAlgorithm(uchar);
    static qint64 currentPeriod();
    static YubiKeyOtp updateOtpResponseFull(const YubiKeyOtp&, const GUtilData*);

    void setIo(YubiKeyIo*);
    void updatePresent();
    OtpList mixOtpLists(OtpList);
    void setOtpList(OtpList);
    void resetOtpList();
    void passwordUpdateStarted();
    void updateTotpTimer();
    void listAndCalculateAll();
    void calculateAll(OtpList);
    YubiKeyOp* reset();
    YubiKeyOp* putToken(const YubiKeyToken&, YubiKeyOp::OpData* aData = Q_NULLPTR);

public Q_SLOTS:
    void onIoStateChanged();
    void onListFinished(uint, const QByteArray&);
    void onCalculateAllFinished(uint, const QByteArray&);
    void onSetCodeFinished(uint, const QByteArray&);
    void onResetFinished(uint, const QByteArray&);
    void onRefreshFinished(uint, const QByteArray&);
    void onPasswordUpdateFinished();
    void onYubiKeyConnected();
    void onYubiKeyIdChanged();
    void onAuthAccessChanged();
    void onTotpTimer();

public:
    QPointer<YubiKeyIo> iIo;
    YubiKeyOpQueue iOpQueue;
    OtpList iOtpList;
    bool iPresent;
    int iPasswordUpdateCount;
    bool iOtpListFetched;
    bool iHaveTotpCodes;
    bool iHaveBeenReset;
    int iTotpTimeLeft;              // seconds
    qint64 iLastRequestedPeriod;    // seconds
    qint64 iLastReceivedPeriod;     // seconds
    QTimer iTotpTimer;
};

/* static */
const YubiKeyPrivateBase::SignalEmitter
YubiKey::Private::gSignalEmitters [] = {
    #define SIGNAL_EMITTER_(Name,name) &YubiKey::name##Changed,
    QUEUED_SIGNALS(SIGNAL_EMITTER_)
    #undef  SIGNAL_EMITTER_
};

/* static */
const YubiKeyIo::APDU YubiKey::Private::LIST_APDU("LIST", 0x00, 0xa1);
const YubiKeyIo::APDU YubiKey::Private::CALCULATE_ALL_APDU("CALCULATE_ALL", 0x00, 0xa4);

YubiKey::Private::Private(
    YubiKey* aYubiKey) :
    YubiKeyPrivateBase(aYubiKey, gSignalEmitters),
    iPresent(false),
    iPasswordUpdateCount(0),
    iOtpListFetched(false),
    iHaveTotpCodes(false),
    iHaveBeenReset(false),
    iTotpTimeLeft(0),
    iLastRequestedPeriod(0),
    iLastReceivedPeriod(0)
{
    iTotpTimer.setSingleShot(true);
    connect(&iTotpTimer, SIGNAL(timeout()), SLOT(onTotpTimer()));

    connect(&iOpQueue, SIGNAL(yubiKeyIdChanged()),
        SLOT(onYubiKeyIdChanged()));
    connect(&iOpQueue, SIGNAL(yubiKeyAuthAccessChanged()),
        SLOT(onAuthAccessChanged()));
    connect(&iOpQueue, SIGNAL(yubiKeyConnected()),
        SLOT(onYubiKeyConnected()));

    connect(&iOpQueue, SIGNAL(yubiKeySerialChanged()),
        aYubiKey, SIGNAL(yubiKeySerialChanged()));
    connect(&iOpQueue, SIGNAL(yubiKeyIdChanged()),
        aYubiKey, SIGNAL(yubiKeyIdChanged()));
    connect(&iOpQueue, SIGNAL(yubiKeyFwVersionChanged()),
        aYubiKey, SIGNAL(yubiKeyVersionChanged()));
    connect(&iOpQueue, SIGNAL(yubiKeyAuthAccessChanged()),
        aYubiKey, SIGNAL(authAccessChanged()));
    connect(&iOpQueue, SIGNAL(yubiKeyConnected()),
        aYubiKey, SIGNAL(yubiKeyConnected()));
    connect(&iOpQueue, SIGNAL(yubiKeyValidationFailed()),
        aYubiKey, SIGNAL(yubiKeyValidationFailed()));
    connect(&iOpQueue, SIGNAL(invalidYubiKeyConnected()),
        aYubiKey, SIGNAL(invalidYubiKeyConnected()));
    connect(&iOpQueue, SIGNAL(restrictedYubiKeyConnected()),
        aYubiKey, SIGNAL(restrictedYubiKeyConnected()));
}

/* static */
YubiKeyTokenType
YubiKey::Private::toAuthType(
    uchar aTypeAlg)
{
    switch (aTypeAlg & TYPE_MASK) {
    case TYPE_HOTP: return YubiKeyTokenType_HOTP;
    case TYPE_TOTP: return YubiKeyTokenType_TOTP;
    }
    return YubiKeyTokenType_Unknown;
}

/* static */
inline
YubiKeyAlgorithm
YubiKey::Private::toAuthAlgorithm(
    uchar aTypeAlg)
{
    return YubiKeyUtil::algorithmFromValue(aTypeAlg & ALG_MASK);
}

/* static */
inline
qint64
YubiKey::Private::currentPeriod()
{
#if HARBOUR_DEBUG
    // Seconds since epoch. Could be produced with e.g.
    // date +%s --date="Feb 27 20:00:16"
    QByteArray value = qgetenv("HARBOUR_YUBIKEY_DATE");
    if (!value.isEmpty()) {
        bool ok = false;
        qint64 secsSinceEpoch = QString::fromLatin1(value).toLongLong(&ok);

        if (ok) {
            return secsSinceEpoch / TOTP_PERIOD_SEC;
        }
    }
#endif
    return QDateTime::currentMSecsSinceEpoch() / (TOTP_PERIOD_SEC * 1000);
}

inline
void
YubiKey::Private::resetOtpList()
{
    setOtpList(OtpList());
}

void
YubiKey::Private::setIo(
    YubiKeyIo* aIo)
{
    if (iIo != aIo) {
        queueSignal(SignalYubiKeyIoChanged);
        if (iIo) {
            iIo->disconnect(this);
        }
        iIo = aIo;
        iOpQueue.setIo(aIo);
        updatePresent();
        if (iHaveBeenReset) {
            iHaveBeenReset = false;
            queueSignal(SignalHaveBeenResetChanged);
        }
        if (iIo) {
            connect(aIo, SIGNAL(ioStateChanged(YubiKeyIo::IoState)),
                SLOT(onIoStateChanged()));
        }
    }
}

void
YubiKey::Private::updatePresent()
{
    const bool present = iIo && iIo->yubiKeyPresent();

    if (iPresent != present) {
        iPresent = present;
        HDEBUG(iPresent);
        queueSignal(SignalPresentChanged);
    }
}

void
YubiKey::Private::passwordUpdateStarted()
{
    if (!(iPasswordUpdateCount++)) {
        queueSignal(SignalUpdatingPasswordsChanged);
    }
    HDEBUG(iPasswordUpdateCount);
}

void
YubiKey::Private::onPasswordUpdateFinished()
{
    HASSERT(iPasswordUpdateCount > 0);
    if (!(--iPasswordUpdateCount)) {
        queueSignal(SignalUpdatingPasswordsChanged);
    }
    HDEBUG(iPasswordUpdateCount);
    emitQueuedSignals();
}

void
YubiKey::Private::onIoStateChanged()
{
    updatePresent();
    emitQueuedSignals();
}

void
YubiKey::Private::onYubiKeyConnected()
{
    HDEBUG("YubiKey" << iIo->ioPath() << "connected");
    listAndCalculateAll();
    emitQueuedSignals();
}

void
YubiKey::Private::onYubiKeyIdChanged()
{
    if (iOtpListFetched) {
        iOtpListFetched = false;
        resetOtpList();
        queueSignal(SignalOtpListFetchedChanged);
        emitQueuedSignals();
    }
}

void
YubiKey::Private::onAuthAccessChanged()
{
    resetOtpList();
    emitQueuedSignals();
}

void
YubiKey::Private::onTotpTimer()
{
    updateTotpTimer();
    emitQueuedSignals();
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
        iTotpTimer.start((int)(nextSecond - msecsSinceEpoch));
    } else {
        iTotpTimeLeft = 0;
        iTotpTimer.stop();
        // Try to refresh the passwords
        if (iIo) {
            switch (iIo->ioState()) {
            case YubiKeyIo::IoTargetInvalid:
            case YubiKeyIo::IoTargetGone:
            case YubiKeyIo::IoError:
                // The I/O in these states is unusable and will never
                // become usable
                break;
            case YubiKeyIo::IoUnknown:
            case YubiKeyIo::IoReady:
            case YubiKeyIo::IoLocking:
            case YubiKeyIo::IoLocked:
            case YubiKeyIo::IoActive:
                // This has a chance to work
                calculateAll(iOtpList);
                break;
            }
        }
    }

    if (lastTotpTimeLeft != iTotpTimeLeft) {
        HDEBUG(iTotpTimeLeft << "sec left");
        queueSignal(SignalTotpTimeLeftChanged);
    }
}

YubiKey::Private::OtpList
YubiKey::Private::mixOtpLists(
    OtpList aList)
{
    // Preserve the existing passwords if the new list doesn't have new ones
    QHash<QByteArray,YubiKeyOtp> map;

    for (OtpListIterator it1(iOtpList); it1.hasNext();) {
        const YubiKeyOtp& otp = it1.next();

        map.insert(otp.iName, otp);
    }

    for (OtpMutableListIterator it2(aList); it2.hasNext();) {
        YubiKeyOtp& otp2 = it2.next();

        if (!otp2.iMiniHash && map.contains(otp2.iName)) {
            const YubiKeyOtp& otp1 = map.value(otp2.iName);

            if (otp2.iType == otp1.iType &&
                otp2.iAlg == otp1.iAlg &&
                otp2.iDigits == otp1.iDigits) {
                otp2.iMiniHash = otp1.iMiniHash;
                HDEBUG(otp2);
            }
        }
    }

    return aList;
}

void
YubiKey::Private::setOtpList(
    OtpList aList)
{
    if (iOtpList != aList) {
        iOtpList = aList;
        queueSignal(SignalOtpListChanged);

        const bool hadTotpCodes = iHaveTotpCodes;
        iHaveTotpCodes = false;
        for (OtpListIterator it(iOtpList); it.hasNext();) {
            if (it.next().iType == YubiKeyTokenType_TOTP) {
                iHaveTotpCodes = true;
                break;
            }
        }
        if (hadTotpCodes != iHaveTotpCodes) {
            queueSignal(SignalHaveTotpCodesChanged);
        }
        if (!iHaveTotpCodes) {
            iTotpTimer.stop();
            if (iTotpTimeLeft) {
                iTotpTimeLeft = 0;
                queueSignal(SignalTotpTimeLeftChanged);
            }
        }
    }
}

/* static */
YubiKeyOtp
YubiKey::Private::updateOtpResponseFull(
    const YubiKeyOtp& aOtp,
    const GUtilData* aData)
{
    GUtilData data = *aData;
    YubiKeyOtp otp(aOtp);

    // +-----------------+----------------------------------------------+
    // | Response tag    | 0x77 for HOTP, 0x7c for touch, 0x75 for full |
    // |                 | response or 0x76 for truncated response      |
    // | Response len    | Length of response + 1                       |
    // | Digits          | Number of digits in the OATH code            |
    // | Response data   | Response                                     |
    // +-----------------+----------------------------------------------+
    otp.iDigits = data.bytes[0];

    // The rest is the calculated hash
    data.bytes++;
    data.size--;

    if (data.size > 0) {
        // Do the dynamic offset truncation per RFC 4226
        gsize off = qMin(gsize(data.bytes[data.size - 1] & 0x0f), data.size - 4);
        otp.iMiniHash = qFromBigEndian(*(quint32*)(data.bytes + off)) & 0x7fffffff;
    }

    HDEBUG(otp);
    return otp;
}

YubiKeyOp*
YubiKey::Private::putToken(
    const YubiKeyToken& aToken,
    YubiKeyOp::OpData* aData)
{
    YubiKeyIo::APDU apdu("PUT", 0x00, 0x01);

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

    // YubiKey seems to require at least 14 bytes of key data
    // Pad the key with zeros if necessary
    const QByteArray secret(aToken.secret());
    const int keySize = qMax(secret.size(), (int) MinKeySize);
    const int padding = keySize - secret.size();
    const QByteArray nameUtf8(YubiKeyUtil::nameToUtf8(aToken.label()));
    uchar type;

    if (aToken.type() == YubiKeyTokenType_HOTP) {
        type = TYPE_HOTP;
        apdu.data.reserve(nameUtf8.size() + keySize + 16);
    } else {
        type = TYPE_TOTP;
        apdu.data.reserve(nameUtf8.size() + keySize + 8);
    }

    apdu.appendTLV(TLV_TAG_NAME, nameUtf8);
    apdu.data.append((char)TLV_TAG_KEY);
    apdu.data.append((char)(keySize + 2));
    apdu.data.append((char)(type | YubiKeyUtil::algorithmValue(aToken.algorithm())));
    apdu.data.append((char)aToken.digits());
    apdu.data.append(secret);
    for (int i = 0; i < padding; i++) {
        apdu.data.append((char)0);
    }

    if (aToken.type() == YubiKeyTokenType_HOTP) {
        const qint32 imf = qToBigEndian<qint32>(aToken.counter());

        apdu.data.append((char)TLV_TAG_PROPERTY);
        apdu.data.append((char)PROP_REQUIRE_TOUCH);
        apdu.appendTLV(TLV_TAG_IMF, sizeof(imf), &imf);
    }

    return iOpQueue.queue(apdu, YubiKeyOpQueue::KeySpecific,
        YubiKeyOpQueue::HighPriority, aData);
}

void
YubiKey::Private::listAndCalculateAll()
{
    YubiKeyOp* listOp = iOpQueue.queue(LIST_APDU, YubiKeyOpQueue::Replace |
        YubiKeyOpQueue::Retry);

    if (listOp) {
        iOpQueue.drop(CALCULATE_ALL_APDU);
        passwordUpdateStarted();
        connect(listOp,
            SIGNAL(destroyed(QObject*)),
            SLOT(onPasswordUpdateFinished()));
        connect(listOp,
            SIGNAL(opFinished(uint,QByteArray)),
            SLOT(onListFinished(uint,QByteArray)));
    }
}

void
YubiKey::Private::calculateAll(
    OtpList aOtpList)
{
    const quint64 challenge = qToBigEndian(iLastRequestedPeriod = currentPeriod());

    // Calculate All Data
    //
    // +-------------------+--------------------------------------------+
    // | Challenge tag     | 0x74 (TLV_TAG_CHALLENGE)                   |
    // | Challenge length  | Length of challenge                        |
    // | Challenge data    | Challenge                                  |
    // +-------------------+--------------------------------------------+
    YubiKeyIo::APDU apdu(CALCULATE_ALL_APDU);

    apdu.data.reserve(CHALLENGE_LEN + 2);
    apdu.appendTLV(TLV_TAG_CHALLENGE, sizeof(challenge), &challenge);

    YubiKeyOp* calculateAllOp = iOpQueue.queue(apdu, YubiKeyOpQueue::Replace,
        new OtpListData(aOtpList));
    if (calculateAllOp) {
        passwordUpdateStarted();
        connect(calculateAllOp,
            SIGNAL(destroyed(QObject*)),
            SLOT(onPasswordUpdateFinished()));
        connect(calculateAllOp,
            SIGNAL(opFinished(uint,QByteArray)),
            SLOT(onCalculateAllFinished(uint,QByteArray)));
    }
}

void
YubiKey::Private::onListFinished(
    uint aResult,
    const QByteArray& aData)
{
    if (aResult == RC_OK) {
        uchar tag;
        GUtilRange resp;
        GUtilData data;
        OtpList list;

        // LIST Response Syntax
        //
        // Response is a continual list of objects looking like:
        // +-----------------+----------------------------------------------+
        // | Name list tag   | 0x72 (TLV_TAG_LIST_ENTRY)                    |
        // | Name length     | Length of name + 1                           |
        // | Algorithm       | High 4 bits is type, low 4 bits is algorithm |
        // | Name data       | Name                                         |
        // +-----------------+----------------------------------------------+
        HDEBUG("Credentials:");
        YubiKeyUtil::initRange(resp, aData);
        while ((tag = YubiKeyUtil::readTLV(&resp, &data)) != 0) {
            if (tag == TLV_TAG_LIST_ENTRY && data.size >= 1) {
                YubiKeyOtp otp(QByteArray((char*)(data.bytes + 1),
                    (int)data.size - 1));

                otp.iType = toAuthType(data.bytes[0]);
                otp.iAlg = toAuthAlgorithm(data.bytes[0]);

                list.append(otp);
                HDEBUG(list.size() << otp);
            }
        }
        if (list.isEmpty()) {
            // No need to request auth data if the list is empty
            if (!iOtpListFetched) {
                iOtpListFetched = true;
                queueSignal(SignalOtpListFetchedChanged);
            }
            resetOtpList();
        } else {
            // Set the initial list (without the passwords) if we don't know
            // anything about the contents of the YubiKey. Otherwise we will
            // update the list when we have the credentials.
            if (iOtpList.isEmpty()) {
                setOtpList(mixOtpLists(list));
            }
            calculateAll(list);
        }
    } else {
        resetOtpList();
    }
    emitQueuedSignals();
}

void
YubiKey::Private::onCalculateAllFinished(
    uint aResult,
    const QByteArray& aData)
{
    if (aResult == RC_OK) {
        iLastReceivedPeriod = iLastRequestedPeriod;

        // Calculate All Response Syntax
        //
        // For HOTP the response tag is 0x77 (No response). For credentials
        // requiring touch the response tag is 0x7c (Response touch).
        // The response will be a list of the following objects:
        //
        // +-----------------+----------------------------------------------+
        // | Name tag        | 0x71 (TLV_TAG_NAME)                          |
        // | Name length     | Length of name                               |
        // | Name data       | Name                                         |
        // +-----------------+----------------------------------------------+
        // | Response tag    | 0x77 for HOTP, 0x7c for touch, 0x75 for full |
        // |                 | response or 0x76 for truncated response      |
        // | Response len    | Length of response + 1                       |
        // | Digits          | Number of digits in the OATH code            |
        // | Response data   | Response                                     |
        // +-----------------+----------------------------------------------+
        OtpList list(senderOpData<OtpListData>()->iOtpList);
        YubiKeyOtp* otp = Q_NULLPTR;
        uchar tag;
        GUtilRange resp;
        GUtilData data;

        YubiKeyUtil::initRange(resp, aData);
        while ((tag = YubiKeyUtil::readTLV(&resp, &data)) != 0) {
            switch (tag) {
            // +---------------+------------------------------------------+
            // | Name tag      | 0x71                                     |
            // | Name length   | Length of name                           |
            // | Name data     | Name                                     |
            // +---------------+------------------------------------------+
            case Private::TLV_TAG_NAME:
                {
                    const QByteArray name(YubiKeyUtil::toByteArray(&data));

                    otp = Q_NULLPTR;
                    for (OtpMutableListIterator it(list); it.hasNext();) {
                        YubiKeyOtp& entry = it.next();

                        if (entry.iName == name) {
                            otp = &entry;
                            break;
                        }
                    }
                    if (!otp) {
                        HDEBUG("Unknown OTP name " << name.constData());
                    }
                }
                break;
            // +---------------+------------------------------------------+
            // | Response tag  | 0x77 for HOTP, 0x7c for touch, 0x75 for  |
            // |               | full response or 0x76 for truncated one  |
            // | Response len  | Length of response + 1                   |
            // | Digits        | Number of digits in the OATH code        |
            // | Response data | Response                                 |
            // +---------------+------------------------------------------+
            case Private::TLV_TAG_NO_RESPONSE:
            case Private::TLV_TAG_RESPONSE_TOUCH:
            case Private::TLV_TAG_RESPONSE_FULL:
                if (otp && data.size > 0) {
                    const YubiKeyOtp newOtp = updateOtpResponseFull(*otp, &data);

                    if (*otp != newOtp) {
                        *otp = newOtp;
                        queueSignal(SignalOtpListChanged);
                    }
                    break;
                }
                /* fallthrough */
            default:
                HDEBUG("Ignoring tag" << hex << tag << qPrintable(YubiKeyUtil::toHex(&data)));
                break;
            }
        }
        if (!iOtpListFetched) {
            iOtpListFetched = true;
            queueSignal(SignalOtpListFetchedChanged);
        }
        setOtpList(mixOtpLists(list));
        updateTotpTimer();
    }
    emitQueuedSignals();
}

YubiKeyOp*
YubiKey::Private::reset()
{
    // Pass the current YubiKeyId to the completion handler so that it can
    // remove the old settings and credentials after a successful reset.
    static const YubiKeyIo::APDU apdu("RESET", 0x00, 0x04, 0xde, 0xad);
    YubiKeyOp* op = iOpQueue.queue(apdu, YubiKeyOpQueue::KeySpecific,
        YubiKeyOpQueue::HighPriority, new BytesData(iOpQueue.yubiKeyId()));

    // Need to reinitialize the whole thing after reset
    connect(op, SIGNAL(opFinished(uint,QByteArray)),
        SLOT(onResetFinished(uint,QByteArray)));
    return op;
}

void
YubiKey::Private::onSetCodeFinished(
    uint aResult,
    const QByteArray&)
{
    if (aResult == RC_OK) {
        // Do not save it yet
        iOpQueue.setPassword(senderOpData<StringData>()->iString, false);
        emitQueuedSignals();
    }
}

void
YubiKey::Private::onResetFinished(
    uint aResult,
    const QByteArray&)
{
    if (aResult == RC_OK) {
        // Remove old credentials and settings
        const QByteArray oldId(senderOpData<BytesData>()->iBytes);
        YubiKeyUtil::configDir(oldId).removeRecursively();
        if (!iHaveBeenReset) {
            iHaveBeenReset = true;
            queueSignal(SignalHaveBeenResetChanged);
        }
        iOpQueue.reinitialize();
        emitQueuedSignals();
    }
}

void
YubiKey::Private::onRefreshFinished(
    uint aResult,
    const QByteArray& aData)
{
    if (aResult == RC_OK) {
        GUtilRange resp;
        GUtilData data;

        YubiKeyUtil::initRange(resp, aData);
        if (YubiKeyUtil::readTLV(&resp, &data) == TLV_TAG_RESPONSE_FULL) {
            HDEBUG("Response:" << qPrintable(YubiKeyUtil::toHex(&data)));
            if (data.size > 4) {
                const QByteArray name(senderOpData<BytesData>()->iBytes);
                const int n = iOtpList.count();

                for (int row = 0; row < n; row++) {
                    const YubiKeyOtp& otp = iOtpList[row];

                    if (otp.iName == name) {
                        const YubiKeyOtp newOtp(updateOtpResponseFull(otp, &data));

                        if (otp != newOtp) {
                            iOtpList[row] = newOtp;
                            queueSignal(SignalOtpListChanged);
                        }
                        break;
                    }
                }
            }
        }
        emitQueuedSignals();
    }
}

// ==========================================================================
// YubiKey
// ==========================================================================

YubiKey::YubiKey(
    QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{}

YubiKey::~YubiKey()
{
    // Ops call back to YubiKey::Private, destroy them while we are still alive
    iPrivate->iOpQueue.clear();
}

YubiKeyIo*
YubiKey::yubiKeyIo() const
{
    return iPrivate->iIo.data();
}

void
YubiKey::setYubiKeyIo(
    YubiKeyIo* aIo)
{
    iPrivate->setIo(aIo);
    iPrivate->emitQueuedSignals();
}

QByteArray
YubiKey::yubiKeyId() const
{
    return iPrivate->iOpQueue.yubiKeyId();
}

QString
YubiKey::yubiKeyIdString() const
{
    return HarbourUtil::toHex(iPrivate->iOpQueue.yubiKeyId());
}

uint
YubiKey::yubiKeySerial() const
{
    return iPrivate->iOpQueue.yubiKeySerial();
}

uint
YubiKey::yubiKeyVersion() const
{
    return YubiKeyUtil::versionToNumber(iPrivate->iOpQueue.yubiKeyFwVersion());
}

QString
YubiKey::yubiKeyVersionString() const
{
    return YubiKeyUtil::versionToString(iPrivate->iOpQueue.yubiKeyFwVersion());
}

YubiKey::AuthAccess
YubiKey::authAccess() const
{
    return (AuthAccess)iPrivate->iOpQueue.yubiKeyAuthAccess();
}

QList<YubiKeyOtp>
YubiKey::otpList() const
{
    return iPrivate->iOtpList;
}

bool
YubiKey::otpListFetched() const
{
    return iPrivate->iOtpListFetched;
}

bool
YubiKey::present() const
{
    return iPrivate->iPresent;
}

bool
YubiKey::updatingPasswords() const
{
    return iPrivate->iPasswordUpdateCount > 0;
}

bool
YubiKey::haveTotpCodes() const
{
    return iPrivate->iHaveTotpCodes;
}

bool
YubiKey::haveBeenReset() const
{
    return iPrivate->iHaveBeenReset;
}

qreal
YubiKey::totpTimeLeft() const
{
    return iPrivate->iTotpTimeLeft;
}

void
YubiKey::clear()
{
    iPrivate->iOpQueue.clear();
}

void
YubiKey::authorize(
    QString aPassword,
    bool aSave)
{
    iPrivate->iOpQueue.setPassword(aPassword, aSave);
}

bool
YubiKey::cancelOp(
    int aOpId)
{
    YubiKeyOp* op = iPrivate->iOpQueue.lookup(aOpId);

    if (op) {
        op->opCancel();
        return true;
    } else {
        return false;
    }
}

YubiKeyOp*
YubiKey::getOp(
    int aOpId)
{
    return iPrivate->iOpQueue.lookup(aOpId);
}

YubiKeyOp*
YubiKey::reset()
{
    return iPrivate->reset();
}

YubiKeyOp*
YubiKey::deleteToken(
    QByteArray aName)
{
    // Delete Data
    //
    // +-----------------+----------------------------------------------+
    // | Name tag        | 0x71 (TLV_TAG_NAME)                          |
    // | Name length     | Length of name data                          |
    // | Name data       | Name                                         |
    // +-----------------+----------------------------------------------+
    YubiKeyIo::APDU apdu("DELETE", 0x00, 0x02);
    apdu.appendTLV(Private::TLV_TAG_NAME, aName);

    YubiKeyOp* op = iPrivate->iOpQueue.queue(apdu, YubiKeyOpQueue::KeySpecific,
        YubiKeyOpQueue::HighPriority);

    // Need to re-read the tokens after deletion
    iPrivate->listAndCalculateAll();
    return op;
}

YubiKeyOp*
YubiKey::refreshToken(
    QByteArray aName)
{
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
    const quint64 challenge = qToBigEndian(Private::currentPeriod());
    YubiKeyIo::APDU apdu("CALCULATE", 0x00, 0xa2);

    apdu.appendTLV(Private::TLV_TAG_NAME, aName);
    apdu.appendTLV(Private::TLV_TAG_CHALLENGE, sizeof(challenge), &challenge);

    YubiKeyOp* op = iPrivate->iOpQueue.queue(apdu, YubiKeyOpQueue::KeySpecific,
        YubiKeyOpQueue::HighPriority, new Private::BytesData(aName));

    // No need to re-read all tokens after calculating the specific one
    iPrivate->connect(op, SIGNAL(opFinished(uint,QByteArray)),
        SLOT(onRefreshFinished(uint,QByteArray)));

    iPrivate->listAndCalculateAll();
    return op;
}

YubiKeyOp*
YubiKey::renameToken(
    QByteArray aFrom,
    QByteArray aTo)
{
    // Rename credential
    //
    // +-----------------+----------------------------------------------+
    // | Name tag        | 0x71 (TLV_TAG_NAME)                          |
    // | Name length     | Length of name data, max 64 bytes            |
    // | Name data       | The current credential's name                |
    // +-----------------+----------------------------------------------+
    // | Name tag        | 0x71 (TLV_TAG_NAME)                          |
    // | Name length     | Length of name data, max 64 bytes            |
    // | Name data       | The new credential's name                    |
    // +-----------------+----------------------------------------------+
    YubiKeyIo::APDU apdu("RENAME", 0x00, 0x05);
    apdu.appendTLV(Private::TLV_TAG_NAME, aFrom);
    apdu.appendTLV(Private::TLV_TAG_NAME, aTo);

    YubiKeyOp* op = iPrivate->iOpQueue.queue(apdu, YubiKeyOpQueue::KeySpecific,
        YubiKeyOpQueue::HighPriority);

    // Re-read the tokens after renaming
    iPrivate->listAndCalculateAll();
    return op;
}

YubiKeyOp*
YubiKey::clearPassword()
{
    return setPassword(QString());
}

YubiKeyOp*
YubiKey::setPassword(
    QString aPassword)
{
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
    YubiKeyIo::APDU apdu("SET_CODE", 0x00, 0x03);

    if (aPassword.isEmpty()) {
        apdu.appendTLV(Private::TLV_TAG_KEY); // Empty
    } else {
        const YubiKeyAlgorithm alg = iPrivate->iOpQueue.yubiKeyAuthAlgorithm();
        const QByteArray challenge(YubiKeyUtil::randomAuthChallenge());
        const QByteArray key(YubiKeyAuth::calculateAccessKey(yubiKeyId(), alg, aPassword));
        const QByteArray response(YubiKeyAuth::calculateResponse(key, challenge, alg));

        HDEBUG("Host challenge:" << challenge.toHex().constData());
        HDEBUG("Response:" << response.toHex().constData());

        // Set Code Data
        //
        // +-------------------+----------------------------------------+
        // | Key tag           | 0x73 (TLV_TAG_KEY)                     |
        // | Key length        | Length of key + 1                      |
        // | Key algorithm     | Algorithm                              |
        // | Key data          | Key                                    |
        // +-------------------+----------------------------------------+
        // | Challenge tag     | 0x74 (TLV_TAG_CHALLENGE)               |
        // | Challenge length  | Length of challenge                    |
        // | Challenge data    | Challenge                              |
        // +-------------------+----------------------------------------+
        // | Response tag      | 0x75 (TLV_TAG_RESPONSE_FULL)           |
        // | Response length   | Length of response                     |
        // | Response data     | Response                               |
        // +-------------------+----------------------------------------+

        apdu.data.reserve(key.size() + Private::CHALLENGE_LEN + response.size() + 7);
        apdu.data.append((char)Private::TLV_TAG_KEY);
        apdu.data.append((char)(key.size() + 1));
        apdu.data.append((char)YubiKeyUtil::algorithmValue(alg));
        apdu.data.append(key);
        apdu.appendTLV(Private::TLV_TAG_CHALLENGE, challenge);
        apdu.appendTLV(Private::TLV_TAG_RESPONSE_FULL, response);
    }

    YubiKeyOp* op = iPrivate->iOpQueue.queue(apdu, YubiKeyOpQueue::KeySpecific,
        YubiKeyOpQueue::HighPriority, new Private::StringData(aPassword));

    iPrivate->connect(op, SIGNAL(opFinished(uint,QByteArray)),
        SLOT(onSetCodeFinished(uint,QByteArray)));
    return op;
}

YubiKeyOp*
YubiKey::putToken(
    int aType,
    int aAlgorithm,
    const QString aLabel,
    const QString aSecret,
    int aDigits,
    int aCounter)
{
    const YubiKeyTokenType type = YubiKeyUtil::validType(aType);
    const YubiKeyAlgorithm alg = YubiKeyUtil::validAlgorithm(aAlgorithm);
    const QByteArray secret(HarbourBase32::fromBase32(aSecret));

    if (type != YubiKeyTokenType_Unknown &&
        alg != YubiKeyAlgorithm_Unknown &&
        !secret.isEmpty()) {
        YubiKeyToken token(type, alg, aLabel, QString(), secret, aDigits, aCounter);
        YubiKeyOp* op = iPrivate->putToken(YubiKeyToken(type, alg, aLabel,
            QString(), secret, aDigits, aCounter));

        // Re-read the tokens after adding new one
        iPrivate->listAndCalculateAll();
        return op;
    } else {
        return Q_NULLPTR;
    }
}

QList<int>
YubiKey::putTokens(
    QList<YubiKeyToken> aTokens)
{
    QList<int> ids;
    const int n = aTokens.count();

    if (n > 0) {
        ids.reserve(n);
        for (int i = 0; i < n; i++) {
            ids.append(iPrivate->putToken(aTokens.at(i))->opId());
            HDEBUG(aTokens.at(i) << "=>" << ids.last());
        }

        // Re-read the tokens after adding new ones
        iPrivate->listAndCalculateAll();
    }
    return ids;
}

void
YubiKey::listAndCalculateAll()
{
    iPrivate->listAndCalculateAll();
}

#include "YubiKey.moc"
