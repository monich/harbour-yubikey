import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

CoverBackground {
    id: cover

    property Page yubiKeyPage

    signal clearCardInfo()

    readonly property bool _haveYubiKey: !!yubiKeyPage
    readonly property int _passwordType: yubiKeyPage ? yubiKeyPage.favoriteTokenType : YubiKeyCard.TypeUnknown
    readonly property real _passwordTimeLeft: yubiKeyPage ? yubiKeyPage.totpTimeLeft : 0
    readonly property bool _passwordMarkedForRefresh: yubiKeyPage && yubiKeyPage.favoriteMarkedForRefresh
    readonly property bool _yubiKeyPresent: yubiKeyPage && yubiKeyPage.yubiKeyPresent

    readonly property string name: yubiKeyPage ? yubiKeyPage.favoriteName : ""
    readonly property string password: yubiKeyPage ? yubiKeyPage.favoritePassword : ""
    readonly property bool accessDenied: yubiKeyPage && yubiKeyPage.yubiKeyAccessDenied

    property string _displayName
    property string _displayPassword
    property bool _displayAccessDenied

    onPasswordChanged: {
        // Don't update non-empty password during flip
        if (_displayPassword === "" || !flipable.flipping) {
            _displayPassword = password
        }
    }

    onNameChanged: {
        // Don't update non-empty password during flip
        if (_displayName === "" || !flipable.flipping) {
            _displayName = accessDenied ? "" : name
        }
    }

    onAccessDeniedChanged: {
        if (!_displayAccessDenied || !flipable.flipping) {
            _displayAccessDenied = accessDenied
        }
    }

    Flipable {
        id: flipable

        readonly property bool flipped: _haveYubiKey
        property bool flipping
        property real targetAngle


        anchors.fill: parent

        readonly property int imageSize: Math.floor(parent.width * 3 / 5) + 2 * Theme.paddingSmall

        onFlippingChanged: {
            if (!flipping) {
                _displayPassword = password
                _displayName = accessDenied ? "" : name
                _displayAccessDenied = accessDenied
            }
        }

        front: Item {
            anchors.fill: parent

            HarbourHighlightIcon {
                id: idleIcon

                y: Theme.itemSizeExtraSmall + yubiKeyProgress.imageMargins
                anchors.horizontalCenter: parent.horizontalCenter
                source: "images/yubikey-mask.svg"
                sourceSize.width: flipable.imageSize - 2 * yubiKeyProgress.imageMargins
                highlightColor: Theme.primaryColor
                opacity: 0.4
            }

            Label {
                width: Math.min(implicitWidth, parent.width - 2 * Theme.paddingLarge)
                anchors {
                    top: idleIcon.bottom
                    topMargin: yubiKeyProgress.imageMargins
                    bottom: parent.bottom
                    bottomMargin: Theme.itemSizeSmall/cover.scale
                    horizontalCenter: parent.horizontalCenter
                }
                font {
                    pixelSize: Theme.fontSizeLarge
                    bold: true
                }
                wrapMode: Text.NoWrap
                fontSizeMode: Text.Fit
                minimumPixelSize: Theme.fontSizeSmall
                truncationMode: TruncationMode.Fade
                verticalAlignment: Text.AlignVCenter
                color: Theme.highlightColor
                opacity: 0.6
                //: Application title
                //% "YubiKey"
                text: qsTrId("yubikey-app_name")
            }
        }

        back: Item {
            anchors.fill: parent

            Label {
                width: Math.min(implicitWidth, parent.width - 2 * Theme.paddingLarge)
                anchors {
                    top: parent.top
                    bottom: yubiKeyProgress.top
                    horizontalCenter: parent.horizontalCenter
                }
                font.pixelSize: Theme.fontSizeMedium
                wrapMode: Text.NoWrap
                fontSizeMode: Text.Fit
                minimumPixelSize: Theme.fontSizeExtraSmall
                truncationMode: TruncationMode.Fade
                verticalAlignment: Text.AlignVCenter
                color: Theme.highlightColor
                text: _displayName
            }

            HarbourHighlightIcon {
                y: Theme.itemSizeExtraSmall + yubiKeyProgress.imageMargins
                anchors.horizontalCenter: parent.horizontalCenter
                sourceSize.width: flipable.imageSize - 2 * yubiKeyProgress.imageMargins
                source: "images/yubikey-lock.svg"
                highlightColor: Theme.primaryColor
                visible: opacity > 0
                opacity: (_yubiKeyPresent ? 1 : 0.4) * (1.0 - yubiKeyProgress.opacity)
            }

            YubiKeyProgress {
                id: yubiKeyProgress

                y: Theme.itemSizeExtraSmall
                width: flipable.imageSize
                height: width
                anchors.horizontalCenter: parent.horizontalCenter
                timeLeft: _passwordType === YubiKeyCard.TypeTOTP ? _passwordTimeLeft : 0
                active: flipable.flipped || flipable.flipping
                visible: opacity > 0
                opacity: _displayAccessDenied ? 0 : 1
                Behavior on opacity { FadeAnimation { } }
            }

            Label {
                id: passwordLabel

                width: Math.min(implicitWidth, parent.width - 2 * Theme.paddingLarge)
                anchors {
                    top: yubiKeyProgress.bottom
                    bottom: parent.bottom
                    bottomMargin: Theme.itemSizeSmall/cover.scale
                    horizontalCenter: parent.horizontalCenter
                }
                font {
                    pixelSize: Theme.fontSizeExtraLarge
                    family: Theme.fontFamilyHeading
                    bold: true
                }
                wrapMode: Text.NoWrap
                fontSizeMode: Text.Fit
                minimumPixelSize: Theme.fontSizeSmall
                truncationMode: TruncationMode.Fade
                verticalAlignment: Text.AlignVCenter
                color: Theme.highlightColor
                opacity: ((_passwordType === YubiKeyCard.TypeHOTP && !_passwordMarkedForRefresh) ||
                          (_passwordType === YubiKeyCard.TypeTOTP && _passwordTimeLeft)) ? 1 : 0.4
                transform: HarbourTextFlip {
                    enabled: !flipable.flipping
                    target: passwordLabel
                    text: (_displayPassword.length > 0 || _passwordType === YubiKeyCard.TypeUnknown) ?
                        _displayPassword : "\u2022 \u2022 \u2022"
                }

                Behavior on opacity { FadeAnimation { duration: 250 } }
            }
        }

        transform: Rotation {
            id: rotation

            origin {
                x: flipable.width/2
                y: flipable.height/2
            }
            axis {
                x: 0
                y: 1
                z: 0
            }
        }

        states: [
            State {
                name: "front"
                when: !flipable.flipped
                PropertyChanges {
                    target: rotation
                    angle: flipable.targetAngle
                }
            },
            State {
                name: "back"
                when: flipable.flipped
                PropertyChanges {
                    target: rotation
                    angle: 180
                }
            }
        ]

        transitions: Transition {
            SequentialAnimation {
                ScriptAction { script: flipable.flipping = true; }
                NumberAnimation {
                    target: rotation
                    property: "angle"
                    duration: 500
                }
                ScriptAction { script: flipable.completeFlip() }
            }
        }

        onFlippedChanged: {
            if (!flipped) {
                targetAngle = 360
            }
        }

        function completeFlip() {
            flipping = false
            if (!flipped) {
                targetAngle = 0
            }
        }
    }

    CoverActionList {
        enabled: _haveYubiKey && !flipable.flipping
        CoverAction {
            iconSource: "image://theme/icon-cover-cancel"
            onTriggered: cover.clearCardInfo()
        }
    }
}
