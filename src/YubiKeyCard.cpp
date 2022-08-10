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

#include <nfcdc_isodep.h>

#include "foil_random.h"

#include "YubiKey.h"
#include "YubiKeyCard.h"
#include "YubiKeyConstants.h"
#include "YubiKeyUtil.h"

#include "HarbourBase32.h"
#include "HarbourDebug.h"

#define YUBIKEY_ACCESS(s) \
    s(Unknown) \
    s(Denied) \
    s(Granted)

#define YUBIKEY_STATES(s) \
    s(Idle) \
    s(Unauthorized) \
    s(Ready)

#if HARBOUR_DEBUG
inline
QDebug
operator<<(
    QDebug aDebug,
    YubiKeyCard::YubiKeyState aState)
{
    switch (aState) {
    #define PRINT_STATE_(s) case YubiKeyCard::YubiKeyState##s: return (aDebug << #s);
    YUBIKEY_STATES(PRINT_STATE_)
    #undef PRINT_STATE_
    }
    return (aDebug << (int)aState);
}
#  define REPORT_ERROR(name,sw,err) \
    ((void)((err) ? HDEBUG(name " error" << (err)->message) : \
    HDEBUG(name " error" << hex << sw)))
#else
#  define REPORT_ERROR(name,sw,err) ((void)0)
#endif // HARBOUR_DEBUG

// ==========================================================================
// YubiKeyCard::Private declaration
// ==========================================================================

// s(SignalName,signalName)
#define PARENT_SIGNALS(s) \
    s(YubiKeyId,yubiKeyId) \
    s(YubiKeyVersion,yubiKeyVersion) \
    s(YubiKeyOtpList,yubiKeyOtpList) \
    s(YubiKeyOtpData,yubiKeyOtpData) \
    s(YubiKeyState,yubiKeyState) \
    s(AuthAccess,authAccess) \
    s(RefreshableTokens,refreshableTokens) \
    s(OperationIds,operationIds) \
    s(Present,present) \
    s(OtpListFetched,otpListFetched) \
    s(TotpTimeLeft,totpTimeLeft) \
    s(TotpValid,totpValid)

class YubiKeyCard::Private : public QObject
{
    Q_OBJECT

    typedef void (YubiKeyCard::*SignalEmitter)();
    typedef uint SignalMask;

    enum Signal {
#define SIGNAL_ENUM_(Name,name) Signal##Name##Changed,
        PARENT_SIGNALS(SIGNAL_ENUM_)
#undef  SIGNAL_ENUM_
        SignalCount
    };

public:
    static const NfcIsoDepApdu CMD_LIST;

public:
    class CardOperation;
    class AuthorizeOperation;
    class ResetOperation;

    Private(YubiKeyCard*);
    ~Private();

    YubiKeyCard* parentObject() const;
    void queueSignal(Signal);
    void emitQueuedSignals();

    void dropYubiKey();
    void setYubiKeyId(const QByteArray);
    void setState(YubiKeyState);
    YubiKeyAuthAccess authAccess();
    bool present();
    bool otpListFetched();
    bool totpValid();
    int totpTimeLeft();
    void updateState();

public Q_SLOTS:
    void onYubiKeyStateChanged();

public:
    SignalMask iQueuedSignals;
    Signal iFirstQueuedSignal;
    YubiKey* iYubiKey;
    YubiKeyState iYubiKeyState;
};

// ==========================================================================
// YubiKeyCard::Private
// ==========================================================================

const NfcIsoDepApdu YubiKeyCard::Private::CMD_LIST = {
    0x00, 0xa1, 0x00, 0x00, { NULL, 0 }, 0
};

YubiKeyCard::Private::Private(
    YubiKeyCard* aParent) :
    QObject(aParent),
    iQueuedSignals(0),
    iFirstQueuedSignal(SignalCount),
    iYubiKey(Q_NULLPTR),
    iYubiKeyState(YubiKeyStateIdle)
{
}

YubiKeyCard::Private::~Private()
{
    // iYubiKey->disconnect(parentObject()) can't be done here
    // because at this point parent is already null.
    HASSERT(!iYubiKey);
}

inline
YubiKeyCard*
YubiKeyCard::Private::parentObject() const
{
    return qobject_cast<YubiKeyCard*>(parent());
}

void
YubiKeyCard::Private::queueSignal(Signal aSignal)
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
YubiKeyCard::Private::emitQueuedSignals()
{
    static const SignalEmitter emitSignal [] = {
#define SIGNAL_EMITTER_(Name,name) &YubiKeyCard::name##Changed,
        PARENT_SIGNALS(SIGNAL_EMITTER_)
#undef  SIGNAL_EMITTER_
    };
    Q_STATIC_ASSERT(G_N_ELEMENTS(emitSignal) == SignalCount);
    if (iQueuedSignals) {
        YubiKeyCard* obj = parentObject();
        // Reset first queued signal before emitting the signals.
        // Signal handlers may emit more signals.
        uint i = iFirstQueuedSignal;
        iFirstQueuedSignal = SignalCount;
        for (; i < SignalCount && iQueuedSignals; i++) {
            const SignalMask signalBit = (SignalMask(1) << i);
            if (iQueuedSignals & signalBit) {
                iQueuedSignals &= ~signalBit;
                Q_EMIT (obj->*(emitSignal[i]))();
            }
        }
    }
}

void
YubiKeyCard::Private::dropYubiKey()
{
    if (iYubiKey) {
        HASSERT(parentObject());
        iYubiKey->disconnect(parentObject());
        iYubiKey->disconnect(this);
        iYubiKey->put();
        iYubiKey = Q_NULLPTR;
    }
}

inline
YubiKeyAuthAccess
YubiKeyCard::Private::authAccess()
{
    return iYubiKey ? iYubiKey->authAccess() : YubiKeyAuthAccessUnknown;
}

inline
bool
YubiKeyCard::Private::present()
{
    return iYubiKey && iYubiKey->present();
}

inline
bool
YubiKeyCard::Private::otpListFetched()
{
    return iYubiKey && iYubiKey->otpListFetched();
}

inline
int
YubiKeyCard::Private::totpTimeLeft()
{
    return iYubiKey ? iYubiKey->totpTimeLeft() : 0;
}

inline
bool
YubiKeyCard::Private::totpValid()
{
    return iYubiKey && iYubiKey->totpValid();
}


void
YubiKeyCard::Private::setYubiKeyId(
    const QByteArray aYubiKeyId)
{

    if (aYubiKeyId.isEmpty()) {
        if (iYubiKey) {
            HDEBUG("");

            if (iYubiKey->present()) {
                queueSignal(SignalPresentChanged);
            }
            if (iYubiKey->totpValid()) {
                queueSignal(SignalTotpValidChanged);
            }
            if (iYubiKey->totpTimeLeft()) {
                queueSignal(SignalTotpTimeLeftChanged);
            }
            if (iYubiKey->otpListFetched()) {
                queueSignal(SignalOtpListFetchedChanged);
            }
            if (!iYubiKey->yubiKeyVersionString().isEmpty()) {
                queueSignal(SignalYubiKeyVersionChanged);
            }
            if (!iYubiKey->otpListString().isEmpty()) {
                queueSignal(SignalYubiKeyOtpListChanged);
            }
            if (!iYubiKey->otpDataString().isEmpty()) {
                queueSignal(SignalYubiKeyOtpDataChanged);
            }
            if (iYubiKey->authAccess() != YubiKeyAuthAccessUnknown) {
                queueSignal(SignalAuthAccessChanged);
            }
            if (!iYubiKey->refreshableTokens().isEmpty()) {
                queueSignal(SignalRefreshableTokensChanged);
            }
            if (!iYubiKey->operationIds().isEmpty()) {
                queueSignal(SignalOperationIdsChanged);
            }
            queueSignal(SignalYubiKeyIdChanged);
            dropYubiKey();
        }
    } else if (!iYubiKey || iYubiKey->yubiKeyId() != aYubiKeyId) {
        const bool wasPresent = present();
        const bool totpWasValid = totpValid();
        const int prevTotpTimeLeft = totpTimeLeft();
        const int otpListWasFetched = otpListFetched();
        const QByteArray prevVersion(iYubiKey ? iYubiKey->yubiKeyVersion() : QByteArray());
        const QByteArray prevOtpList(iYubiKey ? iYubiKey->otpList() : QByteArray());
        const QByteArray prevOtpData(iYubiKey ? iYubiKey->otpData() : QByteArray());
        const QList<int> prevOperationIds(iYubiKey ? iYubiKey->operationIds() : QList<int>());
        const QStringList prevRefreshableTokens(iYubiKey ? iYubiKey->refreshableTokens() : QStringList());
        const YubiKeyAuthAccess prevAuthAccess = authAccess();

        queueSignal(SignalYubiKeyIdChanged);
        dropYubiKey();
        iYubiKey = YubiKey::get(aYubiKeyId);
        HDEBUG(parentObject() << qPrintable(YubiKeyUtil::toHex(aYubiKeyId)) << iYubiKey);

        // Connect signals
        YubiKeyCard* card = parentObject();
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(yubiKeyReset()),
            SIGNAL(yubiKeyReset())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(yubiKeyVersionChanged()),
            SIGNAL(yubiKeyVersionChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(otpDataChanged()),
            SIGNAL(yubiKeyOtpDataChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(otpListChanged()),
            SIGNAL(yubiKeyOtpListChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(otpListFetchedChanged()),
            SIGNAL(otpListFetchedChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(presentChanged()),
            SIGNAL(presentChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(totpValidChanged()),
            SIGNAL(totpValidChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(totpTimeLeftChanged()),
            SIGNAL(totpTimeLeftChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(passwordChanged()),
            SIGNAL(passwordChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(passwordRemoved()),
            SIGNAL(passwordRemoved())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(accessKeyNotAccepted()),
            SIGNAL(accessKeyNotAccepted())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(totpCodesExpired()),
            SIGNAL(totpCodesExpired())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(authAccessChanged()),
            SIGNAL(authAccessChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(refreshableTokensChanged()),
            SIGNAL(refreshableTokensChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(operationIdsChanged()),
            SIGNAL(operationIdsChanged())));
        HVERIFY(card->connect(iYubiKey,
            SIGNAL(operationFinished(int,bool)),
            SIGNAL(operationFinished(int,bool))));
        HVERIFY(connect(iYubiKey,
            SIGNAL(authAccessChanged()),
            SLOT(onYubiKeyStateChanged())));
        HVERIFY(connect(iYubiKey,
            SIGNAL(presentChanged()),
            SLOT(onYubiKeyStateChanged())));

        if (iYubiKey->present() != wasPresent) {
            queueSignal(SignalPresentChanged);
        }
        if (iYubiKey->totpValid() != totpWasValid) {
            queueSignal(SignalTotpValidChanged);
        }
        if (iYubiKey->totpTimeLeft() != prevTotpTimeLeft) {
            queueSignal(SignalTotpTimeLeftChanged);
        }
        if (iYubiKey->otpListFetched() != otpListWasFetched) {
            queueSignal(SignalOtpListFetchedChanged);
        }
        if (iYubiKey->yubiKeyVersion() != prevVersion) {
            queueSignal(SignalYubiKeyVersionChanged);
        }
        if (iYubiKey->otpList() != prevOtpList) {
            queueSignal(SignalYubiKeyOtpListChanged);
        }
        if (iYubiKey->otpData() != prevOtpData) {
            queueSignal(SignalYubiKeyOtpDataChanged);
        }
        if (iYubiKey->operationIds() != prevOperationIds) {
            queueSignal(SignalOperationIdsChanged);
        }
        if (iYubiKey->refreshableTokens() != prevRefreshableTokens) {
            queueSignal(SignalRefreshableTokensChanged);
        }
        if (iYubiKey->authAccess() != prevAuthAccess) {
            queueSignal(SignalAuthAccessChanged);
        }
    }
}

void
YubiKeyCard::Private::setState(
    YubiKeyState aState)
{
    if (iYubiKeyState != aState) {
        HDEBUG(iYubiKeyState << "=>" << aState);
        iYubiKeyState = aState;
        queueSignal(SignalYubiKeyStateChanged);
    }
}

void
YubiKeyCard::Private::updateState()
{
    HDEBUG(parentObject() << iYubiKeyState << authAccess() << iYubiKey->present());
    switch (iYubiKeyState) {
    case YubiKeyStateIdle:
        switch (authAccess()) {
        case YubiKeyAuthAccessUnknown:
            break;
        case YubiKeyAuthAccessDenied:
            setState(YubiKeyStateUnauthorized);
            break;
        case YubiKeyAuthAccessOpen:
        case YubiKeyAuthAccessGranted:
            setState(YubiKeyStateReady);
            break;
        }
        break;
    case YubiKeyStateReady:
        if (authAccess() == YubiKeyAuthAccessDenied) {
            setState(YubiKeyStateUnauthorized);
        }
        break;
    case YubiKeyStateUnauthorized:
        if (authAccess() == YubiKeyAuthAccessGranted) {
            setState(YubiKeyStateReady);
        }
        break;
    }
}

void
YubiKeyCard::Private::onYubiKeyStateChanged()
{
    updateState();
    emitQueuedSignals();
}

// ==========================================================================
// YubiKeyCard
// ==========================================================================

YubiKeyCard::YubiKeyCard(QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{
    HDEBUG(this);
}

YubiKeyCard::~YubiKeyCard()
{
    HDEBUG(this << qPrintable(yubiKeyId()));
    iPrivate->dropYubiKey();
    delete iPrivate;
}

QString
YubiKeyCard::yubiKeyId() const
{
    return iPrivate->iYubiKey ?
        iPrivate->iYubiKey->yubiKeyIdString() :
        QString();
}

void
YubiKeyCard::setYubiKeyId(
    const QString aHexId)
{
    iPrivate->setYubiKeyId(YubiKeyUtil::fromHex(aHexId));
    iPrivate->emitQueuedSignals();
}

YubiKeyCard::AuthAccess
YubiKeyCard::authAccess() const
{
    switch (iPrivate->authAccess()) {
    case YubiKeyAuthAccessDenied: return AccessDenied;
    case YubiKeyAuthAccessOpen: return AccessOpen;
    case YubiKeyAuthAccessGranted: return AccessGranted;
    case YubiKeyAuthAccessUnknown: /* default */ break;
    }
    return AccessUnknown;
}

QList<int>
YubiKeyCard::operationIds() const
{
    return iPrivate->iYubiKey ?
        iPrivate->iYubiKey->operationIds() :
        QList<int>();
}

YubiKeyCard::YubiKeyState
YubiKeyCard::yubiKeyState() const
{
    return iPrivate->iYubiKeyState;
}

QString
YubiKeyCard::yubiKeyVersion() const
{
    return iPrivate->iYubiKey ?
        iPrivate->iYubiKey->yubiKeyVersionString() :
        QString();
}

QString
YubiKeyCard::yubiKeyOtpList() const
{
    return iPrivate->iYubiKey ?
        iPrivate->iYubiKey->otpListString() :
        QString();
}

QString
YubiKeyCard::yubiKeyOtpData() const
{
    return iPrivate->iYubiKey ?
        iPrivate->iYubiKey->otpDataString() :
        QString();
}

QStringList
YubiKeyCard::refreshableTokens() const
{
    return iPrivate->iYubiKey ?
        iPrivate->iYubiKey->refreshableTokens() :
        QStringList();
}

bool
YubiKeyCard::present() const
{
    return iPrivate->present();
}

bool
YubiKeyCard::otpListFetched() const
{
    return iPrivate->otpListFetched();
}

bool
YubiKeyCard::totpValid() const
{
    return iPrivate->totpValid();
}

qreal
YubiKeyCard::totpTimeLeft() const
{
    return iPrivate->totpTimeLeft();
}

int
YubiKeyCard::putToken(
    int aType,
    int aAlgorithm,
    const QString aLabel,
    const QString aSecret,
    int aDigits,
    int aCounter)
{
    if (iPrivate->present()) {
        const YubiKeyTokenType type = YubiKeyUtil::validType(aType);
        const YubiKeyAlgorithm alg = YubiKeyUtil::validAlgorithm(aAlgorithm);
        const QByteArray secret(HarbourBase32::fromBase32(aSecret));

        if (type != YubiKeyTokenType_Unknown &&
            alg != YubiKeyAlgorithm_Unknown &&
            !secret.isEmpty()) {
            QList<YubiKeyToken> list;

            list.append(YubiKeyToken(type, alg, aLabel, QString(),
                secret, aDigits, aCounter));
            return iPrivate->iYubiKey->putTokens(list);
        }
    }
    return 0;
}

int
YubiKeyCard::putTokens(
    const QList<YubiKeyToken> aTokens)
{
    if (iPrivate->present()) {
        return iPrivate->iYubiKey->putTokens(aTokens);
    } else {
        HDEBUG("Can't save tokens (YubiKey is not present)");
        return 0;
    }
}

void
YubiKeyCard::refreshTokens(
    const QStringList aList)
{
    if (iPrivate->iYubiKey) {
        iPrivate->iYubiKey->refreshTokens(aList);
    }
}

void
YubiKeyCard::deleteTokens(
    const QStringList aList)
{
    if (iPrivate->iYubiKey) {
        iPrivate->iYubiKey->deleteTokens(aList);
    }
}

bool
YubiKeyCard::submitPassword(
    const QString aPassword,
    bool aSave)
{
    return iPrivate->iYubiKey &&
        iPrivate->iYubiKey->submitPassword(aPassword, aSave);
}

int
YubiKeyCard::setPassword(
    const QString aPassword)
{
    if (iPrivate->present()) {
        HDEBUG("Changing YubiKey password");
        return iPrivate->iYubiKey->setPassword(aPassword);
    } else {
        HDEBUG("Can't change password (YubiKey is not present)");
        return 0;
    }
}

int
YubiKeyCard::reset()
{
    if (iPrivate->present()) {
        HDEBUG("Resetting YubiKey");
        return iPrivate->iYubiKey->reset();
    } else {
        HDEBUG("Can't reset (YubiKey is not present)");
        return 0;
    }
}

bool
YubiKeyCard::validOperationId(
    int aOperationId)
{
    return aOperationId && operationIds().contains(aOperationId);
}

#include "YubiKeyCard.moc"
