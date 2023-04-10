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
#define KEY_VOLUME_ZOOM             DCONF_KEY("volumeZoom")
#define KEY_WIDE_SCAN               DCONF_KEY("wideScan")
#define KEY_RESOLUTION_4_3          DCONF_KEY("resolution_4_3")  // Width is stored
#define KEY_RESOLUTION_16_9         DCONF_KEY("resolution_16_9") // Width is stored

#define DEFAULT_MAX_ZOOM            10.f
#define DEFAULT_SCAN_ZOOM           3.f
#define DEFAULT_VOLUME_ZOOM         true
#define DEFAULT_WIDE_SCAN           false

// Camera configuration (got removed at some point)
#define CAMERA_DCONF_PATH_(x)           "/apps/jolla-camera/primary/image/" x
#define CAMERA_DCONF_RESOLUTION_4_3     CAMERA_DCONF_PATH_("viewfinderResolution_4_3")
#define CAMERA_DCONF_RESOLUTION_16_9    CAMERA_DCONF_PATH_("viewfinderResolution_16_9")

// ==========================================================================
// YubiKeySettings::Private
// ==========================================================================

class YubiKeySettings::Private
{
public:
    Private(YubiKeySettings* aParent);

    static QSize toSize(const QVariant);
    static QSize size_4_3(int);
    static QSize size_16_9(int);

    QSize resolution_4_3();
    QSize resolution_16_9();

public:
    const int iDefaultResolution_4_3;
    const int iDefaultResolution_16_9;
    MGConfItem* iMaxZoom;
    MGConfItem* iScanZoom;
    MGConfItem* iVolumeZoom;
    MGConfItem* iWideScan;
    MGConfItem* iResolution_4_3;
    MGConfItem* iResolution_16_9;
};

YubiKeySettings::Private::Private(YubiKeySettings* aParent) :
    iDefaultResolution_4_3(toSize(MGConfItem(CAMERA_DCONF_RESOLUTION_4_3).value()).width()),
    iDefaultResolution_16_9(toSize(MGConfItem(CAMERA_DCONF_RESOLUTION_16_9).value()).width()),
    iMaxZoom(new MGConfItem(KEY_MAX_ZOOM, aParent)),
    iScanZoom(new MGConfItem(KEY_SCAN_ZOOM, aParent)),
    iVolumeZoom(new MGConfItem(KEY_VOLUME_ZOOM, aParent)),
    iWideScan(new MGConfItem(KEY_WIDE_SCAN, aParent)),
    iResolution_4_3(new MGConfItem(KEY_RESOLUTION_4_3, aParent)),
    iResolution_16_9(new MGConfItem(KEY_RESOLUTION_16_9, aParent))
{
    connect(iMaxZoom, SIGNAL(valueChanged()), aParent, SIGNAL(maxZoomChanged()));
    connect(iScanZoom, SIGNAL(valueChanged()), aParent, SIGNAL(scanZoomChanged()));
    connect(iVolumeZoom, SIGNAL(valueChanged()), aParent, SIGNAL(volumeZoomChanged()));
    connect(iWideScan, SIGNAL(valueChanged()), aParent, SIGNAL(wideScanChanged()));
    connect(iResolution_4_3, SIGNAL(valueChanged()), aParent, SIGNAL(wideCameraResolutionChanged()));
    connect(iResolution_16_9, SIGNAL(valueChanged()), aParent, SIGNAL(narrowCameraResolutionChanged()));
    HDEBUG("Default 4:3 resolution" << size_4_3(iDefaultResolution_4_3));
    HDEBUG("Default 16:9 resolution" << size_16_9(iDefaultResolution_16_9));
}

QSize
YubiKeySettings::Private::toSize(
    const QVariant aVariant)
{
    // e.g. "1920x1080"
    if (aVariant.isValid()) {
        const QStringList values(aVariant.toString().split('x'));
        if (values.count() == 2) {
            bool ok = false;
            int width = values.at(0).toInt(&ok);
            if (ok && width > 0) {
                int height = values.at(1).toInt(&ok);
                if (ok && height > 0) {
                    return QSize(width, height);
                }
            }
        }
    }
    return QSize(0, 0);
}

QSize
YubiKeySettings::Private::size_4_3(
    int aWidth)
{
    return QSize(aWidth, aWidth * 3 / 4);
}

QSize
YubiKeySettings::Private::size_16_9(
    int aWidth)
{
    return QSize(aWidth, aWidth * 9 / 16);
}

QSize
YubiKeySettings::Private::resolution_4_3()
{
    return size_4_3(qMax(iResolution_4_3->value(iDefaultResolution_4_3).toInt(), 0));
}

QSize
YubiKeySettings::Private::resolution_16_9()
{
    return size_16_9(qMax(iResolution_16_9->value(iDefaultResolution_16_9).toInt(), 0));
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

// volumeZoom

bool
YubiKeySettings::volumeZoom() const
{
    return iPrivate->iVolumeZoom->value(DEFAULT_VOLUME_ZOOM).toBool();
}

void
YubiKeySettings::setVolumeZoom(
    bool aValue)
{
    HDEBUG(aValue);
    iPrivate->iVolumeZoom->set(aValue);
}

// wideScan

bool
YubiKeySettings::wideScan() const
{
    return iPrivate->iWideScan->value(DEFAULT_WIDE_SCAN).toBool();
}

void
YubiKeySettings::setWideScan(
    bool aValue)
{
    iPrivate->iWideScan->set(aValue);
}

// wideCameraRatio

qreal
YubiKeySettings::wideCameraRatio() const
{
    return 4./3;
}

// wideCameraResolution

QSize
YubiKeySettings::wideCameraResolution() const
{
    return iPrivate->resolution_4_3();
}

void
YubiKeySettings::setWideCameraResolution(
    QSize aSize)
{
    HDEBUG(aSize);
    iPrivate->iResolution_4_3->set(aSize.width());
}

// narrowCameraRatio

qreal
YubiKeySettings::narrowCameraRatio() const
{
    return 16./9;
}

QSize
YubiKeySettings::narrowCameraResolution() const
{
    return iPrivate->resolution_16_9();
}

void
YubiKeySettings::setNarrowCameraResolution(
    QSize aSize)
{
    HDEBUG(aSize);
    iPrivate->iResolution_16_9->set(aSize.width());
}
