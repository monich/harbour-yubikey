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

#ifndef _YUBIKEY_OP_QUEUE_H
#define _YUBIKEY_OP_QUEUE_H

#include "YubiKeyIo.h"
#include "YubiKeyOp.h"

#include <QtCore/QList>

// A sequence of YubiKeyOp's. It's responsible for keeping the context
// between the touches.
//
// The card is locked and SELECT is implicitly performed every time
// when the new YubiKeyIo is provided to the queue or when the queue
// becomes non-empty. The queue state becomes QueuePrepare for the
// duration of the initial lock, SELECT (and if necessary, VALIDATE)
//
// The select response/status is the response data and the status
// of the last successfully completed SELECT (i.e. the one completed
// without an I/O error). It's kept until a the next successful SELECT
// response.
//
// New ops can be added to YubiKeyOpQueue while it's active, and they
// will be processed as a part of the active sequence.
//
//                      START
//                      =====
//                        |
//                        V
//                   +-----------+
//    +------------> | QueueIdle | <-------------+
//    |              +-----------+               |
//    |               |    ^   |                 |
//    |             setIo  |  auth               |
//    |               |    |   |                 |
//    |               V    V   V                 |
//    |             +--------------+             |
//    |  setIo ---> | QueuePrepare | <--- setIo  |
//    |    |        +--------------+        |    |
//    |    |         /            \         |    |
//    |    |        /              \        |    |
//    |    |       V                V       |    |
//  +---------------+              +---------------+
//  | QueueBlocked  | -----------> |  QueueActive  |
//  +---------------+              +---------------+
//

class YubiKeyOpQueue :
    public QObject
{
    Q_OBJECT

public:
    enum State {
        QueueIdle,
        QueuePrepare,
        QueueBlocked,
        QueueActive
    };

    enum Priority {
        MinimumPriority,
        DefaultPriority = MinimumPriority,
        HighPriority,
        HighestPriority
    };

    enum Flag {
        Default = 0,
        KeySpecific = 0x01,
        Replace = 0x02,
        Retry = 0x04
    };

    Q_DECLARE_FLAGS(Flags,Flag)

    YubiKeyOpQueue(QObject* aParent = Q_NULLPTR);

    void setIo(YubiKeyIo*);
    void setPassword(QString, bool);
    void reinitialize();
    bool isEmpty() const;
    void clear();
    YubiKeyOp* lookup(int) const;

    // N.B. YubiKeyOps are owned by YubiKeyOpQueue.
    // The ownership of YubiKeyOp::OpData is handed over to YubiKeyOp.
    YubiKeyOp* queue(const YubiKeyIo::APDU&);
    YubiKeyOp* queue(const YubiKeyIo::APDU&, Flags);
    YubiKeyOp* queue(const YubiKeyIo::APDU&, Flags, Priority);
    YubiKeyOp* queue(const YubiKeyIo::APDU&, Flags, YubiKeyOp::OpData*);
    YubiKeyOp* queue(const YubiKeyIo::APDU&, Flags, Priority, YubiKeyOp::OpData*);

    State opQueueState() const;
    QList<int> opIds() const;
    uint yubiKeySerial() const;
    QByteArray yubiKeyId() const;
    QByteArray yubiKeyFwVersion() const;
    YubiKeyAuthAccess yubiKeyAuthAccess() const;
    YubiKeyAlgorithm yubiKeyAuthAlgorithm() const;
    QByteArray calculateAuthAccessKey(QString) const;

Q_SIGNALS:
    void opQueueStateChanged();
    void opIdsChanged();
    void yubiKeyIdChanged();
    void yubiKeySerialChanged();
    void yubiKeyFwVersionChanged();
    void yubiKeyAuthChallengeChanged();
    void yubiKeyAuthAlgorithmChanged();
    void yubiKeyAuthAccessChanged();
    void yubiKeyConnected();
    void yubiKeyValidationFailed();
    void invalidYubiKeyConnected();
    void restrictedYubiKeyConnected();

private:
    class Entry;
    class Private;
    Private* iPrivate;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(YubiKeyOpQueue::Flags)

#endif // _YUBIKEY_OP_QUEUE_H
