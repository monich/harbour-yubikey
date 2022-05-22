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

#include "YubiKeyDefs.h"
#include "YubiKeySettings.h"

#include "HarbourDebug.h"

#include <MGConfItem>

#define DCONF_KEY(x)                YUBIKEY_DCONF_ROOT x
#define KEY_MAX_ZOOM                DCONF_KEY("maxZoom")
#define KEY_SCAN_ZOOM               DCONF_KEY("scanZoom")

#define DEFAULT_MAX_ZOOM            10.f
#define DEFAULT_SCAN_ZOOM           3.f

// ==========================================================================
// YubiKeySettings::Private
// ==========================================================================

class YubiKeySettings::Private
{
public:
    Private(YubiKeySettings* aParent);

    static int validateQrCodeEcLevel(int aValue);

public:
    MGConfItem* iMaxZoom;
    MGConfItem* iScanZoom;
};

YubiKeySettings::Private::Private(YubiKeySettings* aParent) :
    iMaxZoom(new MGConfItem(KEY_MAX_ZOOM, aParent)),
    iScanZoom(new MGConfItem(KEY_SCAN_ZOOM, aParent))
{
    QObject::connect(iMaxZoom, SIGNAL(valueChanged()),
        aParent, SIGNAL(maxZoomChanged()));
    QObject::connect(iScanZoom, SIGNAL(valueChanged()),
        aParent, SIGNAL(scanZoomChanged()));
}

// ==========================================================================
// YubiKeySettings
// ==========================================================================

YubiKeySettings::YubiKeySettings(QObject* aParent) :
    QObject(aParent),
    iPrivate(new Private(this))
{
}

YubiKeySettings::~YubiKeySettings()
{
    delete iPrivate;
}

// Callback for qmlRegisterSingletonType<YubiKeySettings>
QObject*
YubiKeySettings::createSingleton(
    QQmlEngine*,
    QJSEngine*)
{
    return new YubiKeySettings;
}

// scanZoom

qreal
YubiKeySettings::scanZoom() const
{
    return iPrivate->iScanZoom->value(DEFAULT_SCAN_ZOOM).toReal();
}

void
YubiKeySettings::setScanZoom(
    qreal aValue)
{
    HDEBUG(aValue);
    iPrivate->iScanZoom->set(aValue);
}

// maxZoom

qreal
YubiKeySettings::maxZoom() const
{
    return iPrivate->iMaxZoom->value(DEFAULT_MAX_ZOOM).toReal();
}

void
YubiKeySettings::setMaxZoom(
    qreal aValue)
{
    HDEBUG(aValue);
    iPrivate->iMaxZoom->set(aValue);
}
