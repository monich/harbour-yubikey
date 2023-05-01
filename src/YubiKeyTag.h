/*
 * Copyright (C) 2022-2023 Slava Monich <slava@monich.com>
 * Copyright (C) 2022 Jolla Ltd.
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer
 *     in the documentation and/or other materials provided with the
 *     distribution.
 *  3. Neither the names of the copyright holders nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING
 * IN ANY WAY OUT OF THE USE OR INABILITY TO USE THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * any official policies, either expressed or implied.
 */

#ifndef _YUBIKEY_TAG_H
#define _YUBIKEY_TAG_H

#include "nfcdc_types.h"

#include "YubiKeyTypes.h"

#include <QObject>
#include <QString>
#include <QByteArray>

class YubiKeyTag :
    public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(YubiKeyTag)

private:
    YubiKeyTag(const QString);
    ~YubiKeyTag();

public:
    typedef struct _GCancellable Cancellable;

    enum TagState {
        TagNone,            // No NFC tag at all
        TagChecking,        // Checking the tag
        TagUnrecognized,    // Not a YubiKey
        TagYubiKeyReady     // Ready for use
    };

    // Only one operation is active at any point of time.
    // It may (and often does) involve a sequence of commands.
    // Operation::submit() may or may not start the operation right away.
    // When the operation is done, it must call the finished() method
    // to unblock the operation queue.
    class Operation {
    public:
        typedef void (*TransmitDone)(Operation*, const GUtilData*, guint, const GError*);

        Operation* ref();
        void unref();
        int submit(YubiKeyTag*, bool aToFront = false);
        void submitUnique(YubiKeyTag*, bool aToFront = false);
        void cancel();

    protected:
        Operation(const char*, Cancellable*, bool);
        virtual ~Operation();

        virtual void lockFailed(const GError*);
        virtual void selectFailed(guint, const GError*);
        virtual bool startOperation() = 0; // Locked and selected
        virtual void operationCancelled();
        virtual void operationFailed();
        virtual void finished(bool aSuccess = true);

        bool transmit(const NfcIsoDepApdu*);
        bool transmit(const NfcIsoDepApdu*, TransmitDone);

    public:
        class Private;
        Private* const iPrivate;
    };

public:
    static YubiKeyTag* get(const QString);
    void put() { unref(); }

    YubiKeyTag* ref();
    void unref();

    const QString path() const;
    NfcIsoDepClient* isoDep() const;

    TagState tagState() const;
    const QByteArray yubiKeyId() const;
    const QByteArray yubiKeyVersion() const;
    const QByteArray yubiKeyAuthChallenge() const;
    YubiKeyAlgorithm yubiKeyAuthAlgorithm() const;
    uint yubiKeySerial() const;
    bool hasAuthChallenge() const;
    const QList<int> operationIds() const;

    const QString yubiKeyIdString() const;
    const QString yubiKeyVersionString() const;

    bool deactivate();
    int transmit(const NfcIsoDepApdu*, QObject*, const char*,
        Cancellable* aCancel = Q_NULLPTR);

#define YUBIKEY_TRANSMIT_RESP_SLOT(name) \
    void name(const GUtilData*, guint, const GError*)

Q_SIGNALS:
    void tagStateChanged();
    void yubiKeyIdChanged();
    void yubiKeyVersionChanged();
    void yubiKeyAuthChallengeChanged();
    void yubiKeyAuthAlgorithmChanged();
    void yubiKeySerialChanged();
    void operationIdsChanged();
    void operationFinished(int, bool);

private:
    class Initialize;
    class Transmit;
    class Private;
    Private* const iPrivate;
};

#endif // _YUBIKEY_BASE_H
