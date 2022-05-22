import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

CoverBackground {
    id: cover

    property bool haveYubiKey
    property string password
    property string name
    property int passwordType: YubiKeyCard.TypeUnknown
    property real passwordTimeLeft
    property bool passwordMarkedForRefresh

    signal clearCardInfo()

    onPasswordChanged: {
        // Don't update non-empty password during flip
        if (flipable.passwordToShow === "" || !flipable.flipping) {
            flipable.passwordToShow = password
        }
    }

    onNameChanged: {
        // Don't update non-empty password during flip
        if (flipable.nameToShow === "" || !flipable.flipping) {
            flipable.nameToShow = name
        }
    }

    Flipable {
        id: flipable

        readonly property bool flipped: haveYubiKey
        property bool flipping
        property real targetAngle
        property string nameToShow
        property string passwordToShow

        anchors.fill: parent

        readonly property int imageSize: Math.floor(parent.width * 3 / 5) + 2 * Theme.paddingSmall

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
                text: flipable.nameToShow
            }

            YubiKeyProgress {
                id: yubiKeyProgress

                y: Theme.itemSizeExtraSmall
                width: flipable.imageSize
                height: width
                anchors.horizontalCenter: parent.horizontalCenter
                timeLeft: passwordType === YubiKeyCard.TypeTOTP ? passwordTimeLeft : 0
                active: flipable.flipped || flipable.flipping
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
                opacity: ((passwordType === YubiKeyCard.TypeHOTP && !passwordMarkedForRefresh) ||
                          (passwordType === YubiKeyCard.TypeTOTP && passwordTimeLeft)) ? 1 : 0.4
                transform: HarbourTextFlip {
                    enabled: !flipable.flipping
                    target: passwordLabel
                    text: (password.length > 0 || passwordType === YubiKeyCard.TypeUnknown) ?
                        flipable.passwordToShow : "\u2022 \u2022 \u2022"
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

        onFlippingChanged: {
            if (!flipping) {
                passwordToShow = password
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
        enabled: haveYubiKey && !flipable.flipping
        CoverAction {
            iconSource: "image://theme/icon-cover-cancel"
            onTriggered: cover.clearCardInfo()
        }
    }
}
