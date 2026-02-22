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

#ifndef _YUBIKEY_OP_TRACKER_H
#define _YUBIKEY_OP_TRACKER_H

#include "YubiKeyOp.h"

class YubiKeyOpTracker :
    public QObject
{
    Q_OBJECT
    Q_PROPERTY(YubiKeyOp* op READ op WRITE setOp NOTIFY opChanged)
    Q_PROPERTY(int opId READ opId NOTIFY opIdChanged)
    Q_PROPERTY(int opResultCode READ opResultCode NOTIFY opResultCodeChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_ENUMS(State)

public:
    YubiKeyOpTracker(QObject* aParent = Q_NULLPTR);

    enum State {
        None,
        Queued,
        Active,
        Cancelled,
        Finished,
        Failed
    };

    YubiKeyOp* op() const;
    void setOp(YubiKeyOp*);

    State state() const;
    int opId() const;
    int opResultCode() const;

    Q_INVOKABLE void cancelOp();

Q_SIGNALS:
    void opChanged();
    void opIdChanged();
    void opResultCodeChanged();
    void stateChanged();

private:
    class Private;
    Private* iPrivate;
};

Q_DECLARE_METATYPE(YubiKeyOpTracker::State)

#endif // _YUBIKEY_OP_TRACKER_H
