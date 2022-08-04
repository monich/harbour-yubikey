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

#ifndef _YUBIKEY_CONSTANTS_H
#define _YUBIKEY_CONSTANTS_H

#include <QtGlobal>

class YubiKeyConstants
{
public:
    static const uchar TLV_TAG_NAME = 0x71;
    static const uchar TLV_TAG_LIST_ENTRY = 0x72;
    static const uchar TLV_TAG_KEY = 0x73;
    static const uchar TLV_TAG_CHALLENGE = 0x74;
    static const uchar TLV_TAG_RESPONSE_FULL = 0x75;
    static const uchar TLV_TAG_RESPONSE_TRUNCATED = 0x76;
    static const uchar TLV_TAG_NO_RESPONSE = 0x77;
    static const uchar TLV_TAG_PROPERTY = 0x78;
    static const uchar TLV_TAG_VERSION = 0x79;
    static const uchar TLV_TAG_IMF = 0x7a;
    static const uchar TLV_TAG_ALG = 0x7b;
    static const uchar TLV_TAG_RESPONSE_TOUCH = 0x7c;

    static const uchar ALG_HMAC_SHA1 = 0x01;
    static const uchar ALG_HMAC_SHA256 = 0x02;
    static const uchar ALG_HMAC_SHA512 = 0x03;
    static const uchar ALG_MASK = 0x0f;

    static const uchar TYPE_HOTP = 0x10;
    static const uchar TYPE_TOTP = 0x20;
    static const uchar TYPE_MASK = 0xf0;

    static const uchar PROP_REQUIRE_TOUCH = 0x02;

    static const uint TOTP_PERIOD_SEC = 30;

    static const uint ACCESS_KEY_LEN = 16;
    static const uint CHALLENGE_LEN = 8;
    static const uint KEY_ITER_COUNT = 1000;

    #define RC_MORE_DATA(rc) (((rc) & 0xff00) == 0x6100)
    static const uint RC_OK = 0x9000;
    static const uint RC_GENERIC_ERROR = 0x6581;
    static const uint RC_AUTH_NOT_ENABLED = 0x6984;
    static const uint RC_WRONG_SYNTAX = 0x6a80;
};

#endif // _YUBIKEY_CONSTANTS_H
