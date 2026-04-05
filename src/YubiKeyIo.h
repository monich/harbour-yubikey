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

#ifndef _YUBIKEY_IO_H
#define _YUBIKEY_IO_H

#include "YubiKeyTypes.h"

#include <QtCore/QAtomicInt>
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QExplicitlySharedDataPointer>
#include <QtCore/QObject>

class YubiKeyIoTx;

// The idea of YubiKeyIo is to abstract the specific I/O mechanism from
// the rest of the application (i.e. USB vs NFC)
//
// A new instance of YubiKeyIo gets allocated every time the device
// is detected by the OS (plugged in or touched).
//
// The serial becomes known when YubiKeyIo transitions from the Initial
// to the Ready state and isn't expected to change for the rest of the
// object's lifetime.
//
//                       START
//                       =====
//                         |
//                         V
//                    +---------+      +---------------+
//    +-------------- | Unknown | ---> | TargetInvalid |
//    |               +---------+      +---------------+
//    |                 |   |                    |
//    |          +--- lock  |                    |
//    |          |          |                    |
//    |   +------|----------|---------------+    |
//    |   |      |  +-------|--------lock   |    |
//    |   |      |  |       |          |    |    |
//    |   |      |  |       |         +--------+ |
//    |   |      |  |       |   ok -- | Active | |
//    |   |      |  |       |   |     +--------+ |
//    |   |      |  |       |   |      ^    |    |
//    |   |      |  |       |   |     /     |    |
//    |   |      |  |       |   |  transmit |    |
//    V   V      |  |       V   V  /        |    |
//  +=======+    |  |     +---------+       |    |
//  | Error |    |  |     |  Ready  | <---------------+
//  +=======+    |  |     +---------+       |    |    |
//    ^   ^      |  |     /    ^   \        |    |    |
//    |   |      |  |  lock    |    \       |    |    |
//    |   |      |  |   /      |     \      |    |    |
//    |   |      V  V  V       |      V     V    V    |
//    |   |    +---------+     |     +============+   |
//    |   +--- | Locking | --------> | TargetGone |   |
//    |        +---------+     |     +============+   |
//    |           |            |      ^     ^         |
//    |           |         unlock   /      |         |
//    |           |            |    /       |         |
//    |           |         +--------+      |         |
//    |           ok -----> | Locked |      |         |
//    |                     +--------+      |         |
//    |                       |     ^       |         |
//    |                   transmit  |       |         |
//    |                       |     |       |         |
//    |                       V     |       |         |
//    |                     +--------+      |         |
//    +-------------------- | Active | --- ok         |
//                          +--------+                |
//                                |                   |
//                              unlock ---------------+
//
// Error and TargetGone are terminal states. Once a terminal state is reached,
// the object remains in this state for the rest of its life.

class YubiKeyIo :
    public QObject
{
    Q_OBJECT

protected:
    YubiKeyIo(QObject*);

public:
    enum Transport {
        NFC,
        USB
    };

    enum IoState {
        IoUnknown,
        IoReady,
        IoLocking,
        IoLocked,
        IoActive,
        IoTargetInvalid,
        IoTargetGone,
        IoError
    };

    // Shared lock
    class IoLockData
    {
        Q_DISABLE_COPY(IoLockData)
        friend class QExplicitlySharedDataPointer<IoLockData>;
        mutable QAtomicInt ref;

    public:
        virtual ~IoLockData();

    protected:
        IoLockData();
    };
    typedef QExplicitlySharedDataPointer<IoLockData> IoLock;

    // ISO/IEC 7816-4 compliant APDU
    struct APDU {
        const char* name;
        uchar cla;
        uchar ins;
        uchar p1;
        uchar p2;
        QByteArray data;
        uint le;

        APDU(const char*, uchar, uchar, uchar aP1 = 0, uchar aP2 = 0, uint aLe = 0);
        APDU(const char*, uchar, uchar, uchar, uchar, const uchar*, uint, uint aLe = 0);
        APDU(const char*, uchar, uchar, uchar, uchar, const QByteArray&, uint aLe = 0);
        bool equals(const APDU&) const;
        bool sameAs(const APDU&) const;
        void appendTLV(uchar);
        void appendTLV(uchar, uchar, const void*);
        void appendTLV(uchar, const QByteArray&);
    };

    // Interface
    virtual const char* ioPath() const = 0;
    virtual Transport ioTransport() const = 0;
    virtual IoState ioState() const = 0;
    virtual uint ioSerial() const = 0;
    virtual IoLock ioLock() = 0;
    virtual YubiKeyIoTx* ioTransmit(const APDU&) = 0;

    // Utilities
    bool canTransmit() const;
    bool yubiKeyPresent() const;
    static bool isTerminalState(IoState);

Q_SIGNALS:
    void ioStateChanged(YubiKeyIo::IoState /* previous state */);
    void ioSerialChanged();
};

Q_DECLARE_METATYPE(YubiKeyIo::IoState)

// YubiKeyIo implementation derives its own class from YubiKeyIoTx and
// returns a new instance of from each YubiKeyIo::ioTransmit() call
// which successully submits the transaction.
//
// By default, the caller of YubiKeyIo::transmit() is responsible for
// deleting the YubiKeyTx object when it's no longer needed. If that's
// done from the signal handler, HarbourUtil::scheduleDeleteLater() has
// to be used to avoid use-after-free.
//
// Deleting the transaction cancels it.
//
// If the AutoDelete option is set, YubiKeyIoTx must delete itself with
// HarbourUtil::scheduleDeleteLater() when it gets completed, after issuing
// the appropriate signals.

class YubiKeyIoTx :
    public QObject
{
    Q_OBJECT

public:
    enum TxState
    {
        TxPending,
        TxCancelled,
        TxFailed,
        TxFinished
    };

    struct Result
    {
        uint code;

        Result();
        Result(uint);

        uint sw1() const;
        uint sw2() const;
        bool success() const;
        bool moreData(uint* aAmount = Q_NULLPTR) const;

        bool operator==(const Result&) const;
        bool operator!=(const Result&) const;
    };

    virtual TxState txState() const = 0;
    virtual void txSetAutoDelete(bool) = 0;
    virtual void txCancel() = 0;

Q_SIGNALS:
    void txCancelled();
    void txFailed();
    void txFinished(YubiKeyIoTx::Result, QByteArray);

protected:
    YubiKeyIoTx(QObject*);
};

QDebug operator<<(QDebug, const YubiKeyIoTx::Result&);
QDebug operator<<(QDebug aDebug, const YubiKeyIo::IoState&);
Q_DECLARE_METATYPE(YubiKeyIoTx::Result)

#endif // _YUBIKEY_IO_H
