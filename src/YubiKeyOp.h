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

#ifndef _YUBIKEY_OP_H
#define _YUBIKEY_OP_H

#include <QtCore/QObject>

// An asynchronous operation. Operation can be queued and get submitted
// in the order they have been queued (considering the priorities).
// YubiKeyOps are owned by YubiKeyOpQueue.
//
// State diagram:
//
//                      START
//                      =====
//                        |
//                        V
//                   +----------+
//                   | OpQueued | ----------+
//                   +----------+           |
//                     |      ^             |
//                     V      |             V
// +----------+      +----------+     +-------------+
// | OpFailed | <--- | OpActive | --> | OpCancelled |
// +----------+      +----------+     +-------------+
//                        |
//                        V
//                  +------------+
//                  | OpFinished |
//                  +------------+
//
class YubiKeyOp :
    public QObject
{
    Q_OBJECT
    Q_PROPERTY(int opId READ opId CONSTANT)
    Q_PROPERTY(int opState READ opState NOTIFY opStateChanged)
    Q_ENUMS(OpState)

public:
    enum OpState {
        OpQueued,
        OpActive,
        OpCancelled,
        OpFinished,
        OpFailed
    };

    class OpData
    {
        Q_DISABLE_COPY(OpData)
    public:
        OpData();
        virtual ~OpData();
    };

    virtual OpData* opData() const = 0;
    virtual OpState opState() const = 0;
    virtual int opId() const = 0;
    virtual void opCancel() = 0;

    bool isDone() const;

Q_SIGNALS:
    void opFinished(uint, QByteArray);
    void opStateChanged();

protected:
    YubiKeyOp(QObject*);
};

#endif // _YUBIKEY_OP_H
