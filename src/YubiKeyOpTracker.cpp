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

#include "YubiKeyOpTracker.h"

#include "YubiKeyIo.h"

#include "HarbourParentSignalQueueObject.h"
#include "HarbourDebug.h"

#include <QtCore/QPointer>

// s(SignalName,signalName)
#define QUEUED_SIGNALS(s) \
    s(Op,op) \
    s(OpId,opId) \
    s(OpResultCode,opResultCode) \
    s(State,state)

#if HARBOUR_DEBUG
QDebug
operator<<(
    QDebug aDebug,
    YubiKeyOpTracker::State aState)
{
    #define STATES(s) s(None) s(Queued) s(Active) s(Cancelled) s(Finished) s(Failed)
    switch (aState) {
    #define STATE_(s) case YubiKeyOpTracker::s: return (aDebug << #s);
    STATES(STATE_)
    #undef STATE_
    }
    return aDebug << (int)aState;
}
#endif // HARBOUR_DEBUG

// ==========================================================================
// YubiKeyOpTracker::Private
// ==========================================================================

enum YubiKeyOpTrackerSignal {
    #define SIGNAL_ENUM_(Name,name) Signal##Name##Changed,
    QUEUED_SIGNALS(SIGNAL_ENUM_)
    #undef  SIGNAL_ENUM_
    YubiKeyOpTrackerSignalCount
};

typedef HarbourParentSignalQueueObject<YubiKeyOpTracker,
    YubiKeyOpTrackerSignal, YubiKeyOpTrackerSignalCount>
    YubiKeyOpTrackerPrivateBase;

class YubiKeyOpTracker::Private :
    public YubiKeyOpTrackerPrivateBase
{
    Q_OBJECT
    static const SignalEmitter gSignalEmitters[];

private Q_SLOTS:
    void onOpFinished(uint, const QByteArray&);
    void onOpStateChanged();
    void onOpDestroyed(QObject*);

public:
    Private(YubiKeyOpTracker*);

    void updateState();
    void setOp(YubiKeyOp*);
    void setOpResult(uint);
    void cancel();

public:
    QPointer<YubiKeyOp> iOp;
    State iState;
    int iOpId;
    uint iOpResultCode;
};

/* static */
const YubiKeyOpTracker::Private::SignalEmitter
YubiKeyOpTracker::Private::gSignalEmitters [] = {
    #define SIGNAL_EMITTER_(Name,name) &YubiKeyOpTracker::name##Changed,
    QUEUED_SIGNALS(SIGNAL_EMITTER_)
    #undef  SIGNAL_EMITTER_
};

YubiKeyOpTracker::Private::Private(
    YubiKeyOpTracker* aTracker) :
    YubiKeyOpTrackerPrivateBase(aTracker, gSignalEmitters),
    iState(None),
    iOpId(0),
    iOpResultCode(0)
{}

void
YubiKeyOpTracker::Private::updateState()
{
    State state = None;

    if (iOp) {
        switch (iOp->opState()) {
        case YubiKeyOp::OpQueued:
            state = Queued;
            break;
        case YubiKeyOp::OpActive:
            state = Active;
            break;
        case YubiKeyOp::OpCancelled:
            state = Cancelled;
            break;
        case YubiKeyOp::OpFinished:
            state = Finished;
            break;
        case YubiKeyOp::OpFailed:
            state = Failed;
            break;
        }
    } else {
        switch (iState) {
        case None:
            break;
        case Active:
            // Lost track of an active op
            state = Cancelled;
            break;
        case Queued:
        case Cancelled:
        case Finished:
        case Failed:
            // Keep the last state after the operation has been destroyed
            return;
        }
    }

    if (iState != state) {
        HDEBUG(iOpId << iState << "=>" << state);
        iState = state;
        queueSignal(SignalStateChanged);
    }
}

void
YubiKeyOpTracker::Private::setOp(
    YubiKeyOp* aOp)
{
    if (iOp != aOp) {
        if (iOp) {
            iOp->disconnect(this);
        }
        iOp = aOp;
        queueSignal(SignalOpChanged);
        setOpResult(0);
        updateState();
        if (aOp) {
            iOpId = aOp->opId();
            queueSignal(SignalOpIdChanged);
            connect(aOp,
                SIGNAL(opFinished(uint,QByteArray)),
                SLOT(onOpFinished(uint,QByteArray)));
            connect(aOp,
                SIGNAL(opStateChanged()),
                SLOT(onOpStateChanged()));
            connect(aOp,
                SIGNAL(destroyed(QObject*)),
                SLOT(onOpDestroyed(QObject*)));
        }
    }
}

void
YubiKeyOpTracker::Private::setOpResult(
    uint aCode)
{
    if (iOpResultCode != aCode) {
        iOpResultCode = aCode;
        queueSignal(SignalOpResultCodeChanged);
    }
}

void
YubiKeyOpTracker::Private::cancel()
{
    YubiKeyOp* op = iOp.data();

    if (op) {
        iOp.clear();
        op->opCancel();
        updateState();
    }
}

void
YubiKeyOpTracker::Private::onOpFinished(
    uint aResult,
    const QByteArray& aData)
{
    setOpResult(aResult);
    emitQueuedSignals();
}

void
YubiKeyOpTracker::Private::onOpStateChanged()
{
    updateState();
    emitQueuedSignals();
}

void
YubiKeyOpTracker::Private::onOpDestroyed(QObject*)
{
    queueSignal(SignalOpChanged);
    updateState();
    emitQueuedSignals();
}

// ==========================================================================
// YubiKeyOpTracker
// ==========================================================================

YubiKeyOpTracker::YubiKeyOpTracker(
    QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{}

YubiKeyOp*
YubiKeyOpTracker::op() const
{
    return iPrivate->iOp;
}

void
YubiKeyOpTracker::setOp(
    YubiKeyOp* aOp)
{
    iPrivate->setOp(aOp);
    iPrivate->emitQueuedSignals();
}

YubiKeyOpTracker::State
YubiKeyOpTracker::state() const
{
    return iPrivate->iState;
}

int
YubiKeyOpTracker::opId() const
{
    return iPrivate->iOpId;
}

int
YubiKeyOpTracker::opResultCode() const
{
    return iPrivate->iOpResultCode;
}

void
YubiKeyOpTracker::cancelOp()
{
    iPrivate->cancel();
    iPrivate->emitQueuedSignals();
}

#include "YubiKeyOpTracker.moc"
