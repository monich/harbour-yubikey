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

#ifndef _YUBIKEY_SETTINGS_H
#define _YUBIKEY_SETTINGS_H

#include <QObject>
#include <QSize>

class QQmlEngine;
class QJSEngine;

class YubiKeySettings :
    public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(YubiKeySettings)
    Q_PROPERTY(qreal scanZoom READ scanZoom WRITE setScanZoom NOTIFY scanZoomChanged)
    Q_PROPERTY(qreal maxZoom READ maxZoom WRITE setMaxZoom NOTIFY maxZoomChanged)
    Q_PROPERTY(bool volumeZoom READ volumeZoom WRITE setVolumeZoom NOTIFY volumeZoomChanged)
    Q_PROPERTY(bool wideScan READ wideScan WRITE setWideScan NOTIFY wideScanChanged)
    Q_PROPERTY(qreal wideCameraRatio READ wideCameraRatio CONSTANT)
    Q_PROPERTY(qreal narrowCameraRatio READ narrowCameraRatio CONSTANT)
    Q_PROPERTY(QSize wideCameraResolution READ wideCameraResolution WRITE setWideCameraResolution NOTIFY wideCameraResolutionChanged)
    Q_PROPERTY(QSize narrowCameraResolution READ narrowCameraResolution WRITE setNarrowCameraResolution NOTIFY narrowCameraResolutionChanged)

public:
    explicit YubiKeySettings(QObject* aParent = Q_NULLPTR);
    ~YubiKeySettings();

    // Callback for qmlRegisterSingletonType<YubiKeySettings>
    static QObject* createSingleton(QQmlEngine*, QJSEngine*);

    qreal scanZoom() const;
    void setScanZoom(qreal);

    qreal maxZoom() const;
    void setMaxZoom(qreal);

    bool volumeZoom() const;
    void setVolumeZoom(bool);

    bool wideScan() const;
    void setWideScan(bool);

    qreal wideCameraRatio() const;
    QSize wideCameraResolution() const;
    void setWideCameraResolution(QSize);

    qreal narrowCameraRatio() const;
    QSize narrowCameraResolution() const;
    void setNarrowCameraResolution(QSize);

Q_SIGNALS:
    void maxZoomChanged();
    void scanZoomChanged();
    void volumeZoomChanged();
    void wideScanChanged();
    void wideCameraResolutionChanged();
    void narrowCameraResolutionChanged();

private:
    class Private;
    Private* iPrivate;
};

#endif // _YUBIKEY_SETTINGS_H
