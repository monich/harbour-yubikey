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

#ifndef _YUBIKEY_USB_IO_H
#define _YUBIKEY_USB_IO_H

struct libusb_context;
struct libusb_device;

#include "YubiKeyIo.h"

class YubiKeyUsbIo :
    public YubiKeyIo
{
    Q_OBJECT

public:
    struct IntfAlt {
        int iIntfNum;
        int iAltSetting;
    };

    class Context
    {
    public:
        Context();
        Context(const Context&);
        ~Context();

        Context& operator = (const Context&);
        operator struct libusb_context*() const;

    private:
        class Private;
        Private* iPrivate;
    };

    YubiKeyUsbIo(Context, libusb_device*, IntfAlt, QObject*);
    ~YubiKeyUsbIo();

    void gone();

    // YubiKeyIo
    const char* ioPath() const Q_DECL_OVERRIDE;
    Transport ioTransport() const Q_DECL_OVERRIDE;
    IoState ioState() const Q_DECL_OVERRIDE;
    uint ioSerial() const Q_DECL_OVERRIDE;
    IoLock ioLock() Q_DECL_OVERRIDE;
    YubiKeyIoTx* ioTransmit(const APDU&) Q_DECL_OVERRIDE;

private:
    class Tx;
    class Lock;
    class Handle;
    class Private;
    Private* iPrivate;
};

#endif // _YUBIKEY_USB_IO_H
