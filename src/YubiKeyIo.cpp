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

#include "YubiKeyIo.h"

#include "YubiKeyConstants.h"
#include "YubiKeyUtil.h"

#include "HarbourDebug.h"
#include "HarbourUtil.h"

// ==========================================================================
// YubiKeyIoTx
// ==========================================================================

YubiKeyIoTx::YubiKeyIoTx(
    QObject* aParent) :
    QObject(aParent)
{}

// ==========================================================================
// YubiKeyIoTx::Result
// ==========================================================================

YubiKeyIoTx::Result::Result() :
    code(0)
{}

YubiKeyIoTx::Result::Result(
    uint aCode) :
    code(aCode)
{}

uint
YubiKeyIoTx::Result::sw1() const
{
    return (code >> 8) & 0xff;
}

uint
YubiKeyIoTx::Result::sw2() const
{
    return code & 0xff;
}

bool
YubiKeyIoTx::Result::success() const
{
    return code == YubiKeyConstants::RC_OK;
}

bool
YubiKeyIoTx::Result::moreData(
    uint* aAmount) const
{
    if (RC_MORE_DATA(code)) {
        if (aAmount) {
            const uint amount = sw2();

            *aAmount = amount ? amount : 0x100;
        }
        return true;
    } else {
        if (aAmount) {
            aAmount = 0;
        }
        return false;
    }
}

bool
YubiKeyIoTx::Result::operator==(
    const Result& aResult) const
{
    return code == aResult.code;
}

bool
YubiKeyIoTx::Result::operator!=(
    const Result& aResult) const
{
    return code != aResult.code;
}

#if HARBOUR_DEBUG
QDebug
operator<<(
    QDebug aDebug,
    const YubiKeyIoTx::Result& aResult)
{
    char sw[2];

    sw[0] = aResult.sw1();
    sw[1] = aResult.sw2();
    return (aDebug << qPrintable(HarbourUtil::toHex(sw, sizeof(sw)).toUpper()));
}
#endif

// ==========================================================================
// YubiKeyIo::IoLock
// ==========================================================================

YubiKeyIo::IoLock::~IoLock()
{}

YubiKeyIo::IoLock::IoLock() :
    ref(0)
{}

// ==========================================================================
// YubiKeyIo::APDU
// ==========================================================================

YubiKeyIo::APDU::APDU(
    const char* aName,
    uchar aCla,
    uchar aIns,
    uchar aP1,
    uchar aP2,
    uchar aLe) :
    name(aName),
    cla(aCla),
    ins(aIns),
    p1(aP1),
    p2(aP2),
    le(aLe)
{}

YubiKeyIo::APDU::APDU(
    const char* aName,
    uchar aCla,
    uchar aIns,
    uchar aP1,
    uchar aP2,
    const uchar* aData,
    uint aSize,
    uchar aLe) :
    name(aName),
    cla(aCla),
    ins(aIns),
    p1(aP1),
    p2(aP2),
    data((char*)aData, aSize),
    le(aLe)
{}

YubiKeyIo::APDU::APDU(
    const char* aName,
    uchar aCla,
    uchar aIns,
    uchar aP1,
    uchar aP2,
    const QByteArray& aData,
    uchar aLe) :
    name(aName),
    cla(aCla),
    ins(aIns),
    p1(aP1),
    p2(aP2),
    data(aData),
    le(aLe)
{}

bool
YubiKeyIo::APDU::equals(
    const APDU& aApdu) const
{
    // Compare everything
    return sameAs(aApdu) &&
        le == aApdu.le &&
        data == aApdu.data;
}

bool
YubiKeyIo::APDU::sameAs(
    const APDU& aApdu) const
{
    // Not comparing the data and Le
    return cla == aApdu.cla &&
        ins == aApdu.ins &&
        p1 == aApdu.p1 &&
        p2 == aApdu.p2;
}

void
YubiKeyIo::APDU::appendTLV(
    uchar aTag)
{
    // Empty TLV
    data.append((char)aTag);
    data.append((char)0);
}

void
YubiKeyIo::APDU::appendTLV(
    uchar aTag,
    const QByteArray& aValue)
{
    HASSERT(aValue.size() <= 0xff);
    data.append((char)aTag);
    data.append((char)aValue.size());
    data.append(aValue);
}

void
YubiKeyIo::APDU::appendTLV(
    uchar aTag,
    uchar aLength,
    const void* aValue)
{
    data.append((char)aTag);
    data.append((char)aLength);
    data.append((const char*)aValue, aLength);
}

// ==========================================================================
// YubiKeyIo
// ==========================================================================

YubiKeyIo::YubiKeyIo(
    QObject* aParent) :
    QObject(aParent)
{}

bool
YubiKeyIo::canTransmit() const
{
    switch (ioState()) {
    case IoReady:
    case IoLocked:
    case IoActive:
        return true;
    case IoUnknown:
    case IoTargetInvalid:
    case IoTargetGone:
    case IoLocking:
    case IoError:
        break;
    }
    return false;
}

bool
YubiKeyIo::yubiKeyPresent() const
{
    switch (ioState()) {
    case IoError:
    case IoTargetGone:
    case IoTargetInvalid:
        break;
    case IoUnknown:
    case IoReady:
    case IoLocking:
    case IoLocked:
    case IoActive:
        return true;
    }
    return false;
}

/* static */
bool
YubiKeyIo::isTerminalState(
    IoState aState)
{
    switch (aState) {
    case IoError:
    case IoTargetGone:
        return true;
    case IoUnknown:
    case IoReady:
    case IoLocking:
    case IoLocked:
    case IoActive:
    case IoTargetInvalid:
        break;
    }
    return false;
}

#if HARBOUR_DEBUG
#define IO_STATES(s) \
    s(IoUnknown) \
    s(IoReady) \
    s(IoLocking) \
    s(IoLocked) \
    s(IoActive) \
    s(IoTargetInvalid) \
    s(IoError) \
    s(IoTargetGone)

QDebug
operator<<(
    QDebug aDebug,
    const YubiKeyIo::IoState& aState)
{
    switch (aState) {
#define STATE_(state) case YubiKeyIo::state: \
    return (aDebug << #state);
        IO_STATES(STATE_)
#undef STATE_
    }
    return (aDebug << (int)aState);
}
#endif // HARBOUR_DEBUG
