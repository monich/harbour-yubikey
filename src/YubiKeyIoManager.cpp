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

#include "nfcdc_default_adapter.h"
#include "nfcdc_tag.h"

#include "YubiKeyIoManager.h"

#include "YubiKeyNfcIo.h"

#include "HarbourDebug.h"
#include "HarbourUtil.h"

// ==========================================================================
// YubiKeyIoManager::Private
// ==========================================================================

class YubiKeyIoManager::Private:
    public QObject
{
    Q_OBJECT

    enum AdapterEvents {
        ADAPTER_EVENT_VALID,
        ADAPTER_EVENT_TAGS,
        ADAPTER_EVENT_COUNT
    };

    enum TagEvents {
        TAG_EVENT_VALID,
        TAG_EVENT_PRESENT,
        TAG_EVENT_COUNT
    };

public:
    Private(YubiKeyIoManager*);
    ~Private();

    static void adapterEvent(NfcDefaultAdapter*, NFC_DEFAULT_ADAPTER_PROPERTY, void*);
    static void tagEvent(NfcTagClient*, NFC_TAG_PROPERTY, void*);

    YubiKeyIoManager* manager() const;
    void dropTag();
    void checkTags();

public:
    NfcDefaultAdapter* iAdapter;
    gulong iAdapterEventIds[ADAPTER_EVENT_COUNT];
    gulong iTagEventIds[TAG_EVENT_COUNT];
    NfcTagClient* iTag;
    YubiKeyIo* iNfcIo;
};

YubiKeyIoManager::Private::Private(
    YubiKeyIoManager* aParent) :
    QObject(aParent),
    iAdapter(nfc_default_adapter_new()),
    iTag(Q_NULLPTR),
    iNfcIo(Q_NULLPTR)
{
    memset(iTagEventIds, 0, sizeof(iTagEventIds));
    iAdapterEventIds[ADAPTER_EVENT_VALID] =
        nfc_default_adapter_add_property_handler(iAdapter,
            NFC_DEFAULT_ADAPTER_PROPERTY_VALID, adapterEvent, this);
    iAdapterEventIds[ADAPTER_EVENT_TAGS] =
        nfc_default_adapter_add_property_handler(iAdapter,
            NFC_DEFAULT_ADAPTER_PROPERTY_TAGS, adapterEvent, this);
    checkTags();
}

YubiKeyIoManager::Private::~Private()
{
    dropTag();
    nfc_default_adapter_remove_all_handlers(iAdapter, iAdapterEventIds);
    nfc_default_adapter_unref(iAdapter);
}

inline
YubiKeyIoManager*
YubiKeyIoManager::Private::manager() const
{
    return qobject_cast<YubiKeyIoManager*>(parent());
}

/* static  */
void
YubiKeyIoManager::Private::adapterEvent(
    NfcDefaultAdapter*,
    NFC_DEFAULT_ADAPTER_PROPERTY,
    void* aPrivate)
{
    ((Private*) aPrivate)->checkTags();
}

/* static  */
void
YubiKeyIoManager::Private::tagEvent(
    NfcTagClient*,
    NFC_TAG_PROPERTY,
    void* aPrivate)
{
    ((Private*) aPrivate)->checkTags();
}

void
YubiKeyIoManager::Private::dropTag()
{
    if (iTag) {
        nfc_tag_client_remove_all_handlers(iTag, iTagEventIds);
        nfc_tag_client_unref(iTag);
        iTag = Q_NULLPTR;
    }
}

void
YubiKeyIoManager::Private::checkTags()
{
    if (iAdapter->valid) {
        const char* activeTag = iAdapter->tags[0];

        if (activeTag) {
            if (iTag && strcmp(iTag->path, activeTag)) {
                dropTag();
            }
            if (!iTag) {
                iTag = nfc_tag_client_new(activeTag);
                iTagEventIds[TAG_EVENT_VALID] =
                    nfc_tag_client_add_property_handler(iTag,
                        NFC_TAG_PROPERTY_VALID, tagEvent, this);
                iTagEventIds[TAG_EVENT_PRESENT] =
                    nfc_tag_client_add_property_handler(iTag,
                        NFC_TAG_PROPERTY_PRESENT, tagEvent, this);
            }
        } else {
            dropTag();
        }

        // YubiKeyNfcIo gets created when the tag is valid and present
        if (iTag && iTag->valid && iTag->present) {
            if (!iNfcIo || strcmp(iNfcIo->ioPath(), activeTag)) {
                if (iNfcIo) {
                    HDEBUG(iNfcIo->ioPath() << "is gone");
                    HarbourUtil::scheduleDeleteLater(iNfcIo);
                }
                HDEBUG("Creating" << activeTag);
                iNfcIo = new YubiKeyNfcIo(iTag, this);
                Q_EMIT manager()->yubiKeyIoChanged();
            }
        } else if (iNfcIo) {
            HDEBUG(iNfcIo->ioPath() << "is gone");
            HarbourUtil::scheduleDeleteLater(iNfcIo);
            iNfcIo = Q_NULLPTR;
            Q_EMIT manager()->yubiKeyIoChanged();
        }
    }
}

// ==========================================================================
// YubiKeyIoManager
// ==========================================================================

YubiKeyIoManager::YubiKeyIoManager(
    QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{}

YubiKeyIo*
YubiKeyIoManager::yubiKeyIo() const
{
    return iPrivate->iNfcIo;
}

#include "YubiKeyIoManager.moc"
