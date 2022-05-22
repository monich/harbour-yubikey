import QtQuick 2.0
import Sailfish.Silica 1.0
import org.nemomobile.notifications 1.0
import harbour.yubikey 1.0

Page {
    id: thisPage

    backNavigation: false
    showNavigationIndicator: false

    property Page yubiKeyPage

    readonly property bool _targetPresent: NfcAdapter.targetPresent
    property bool _initialized

    onStatusChanged: {
        if (status === PageStatus.Active) {
            _initialized = true
            if (YubiKeyRecognizer.yubiKeyId === "") {
                if (yubiKeyPage) {
                    yubiKeyPage = null
                }
            } else {
                checkYubiKeyPage()
            }
        }
    }

    function checkYubiKeyPage() {
        if (YubiKeyRecognizer.yubiKeyId !== "") {
            if (!yubiKeyPage) {
                createYubiKeyPage(YubiKeyRecognizer.yubiKeyId, function(prop) {
                    return pageStack.push(yubiKeyPageComponent, prop)
                })
            } else if (yubiKeyPage.yubiKeyId !== YubiKeyRecognizer.yubiKeyId) {
                replaceYubiKeyPage(YubiKeyRecognizer.yubiKeyId)
            } else {
                YubiKeyRecognizer.clearState()
            }
        }
    }

    function replaceYubiKeyPage(cardId) {
        createYubiKeyPage(cardId, function(prop) {
            return pageStack.replaceAbove(thisPage, yubiKeyPageComponent, prop)
        })
    }

    function createYubiKeyPage(cardId,action) {
        YubiKeyRecognizer.clearState()
        var prevYubiKeyPage = yubiKeyPage
        var newYubiKeyPage = yubiKeyPage = action({
            "allowedOrientations": thisPage.allowedOrientations,
            "yubiKeyId": cardId
        })
        yubiKeyPage.replacePage.connect(replaceYubiKeyPage)
        if (prevYubiKeyPage) {
            newYubiKeyPage.statusChanged.connect(function() {
                if (newYubiKeyPage.status === PageStatus.Inactive) {
                    // Replaced page doesn't always get deallocated,
                    // disconnect useless signals in case if it doesn't
                    prevYubiKeyPage.yubiKeyId = ""
                }
            })
        }
        newYubiKeyPage.yubiKeyReset.connect(function() {
            //: Pop-up notification
            //% "YubiKey has been reset"
            notification.previewBody = qsTrId("yubikey-notification-reset")
            notification.publish()
        })
    }

    Component {
        id: yubiKeyPageComponent

        YubiKeyPage {
        }
    }

    Connections {
        target: YubiKeyRecognizer
        onYubiKeyIdChanged: checkYubiKeyPage()
    }

    Notification {
        id: notification

        expireTimeout: 2000
        Component.onCompleted: {
            if ('icon' in notification) {
                notification.icon = Qt.resolvedUrl("images/yubikey.svg")
            }
        }
    }

    Item {
        width: isPortrait ? Screen.width : Screen.height
        height: isPortrait ? Screen.height : Screen.width
        visible: NfcSystem.valid

        BusyIndicator {
            id: busyIndicator

            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: _targetPresent
            visible: opacity > 0
        }

        YubiKeyImage {
            id: image

            width: height
            height: Screen.width
            opacity: label.opacity
        }

        InfoLabel {
            id: label

            anchors {
                bottom: parent.bottom
                right: parent.right
                rightMargin: Theme.horizontalPageMargin
            }
            verticalAlignment: Text.AlignVCenter
            opacity: (_initialized && !busyIndicator.running) ? 1 : 0
            width: isPortrait ? (parent.width - 2 * Theme.horizontalPageMargin) :
                (parent.width - image.width - Theme.horizontalPageMargin)
            height: isPortrait ? (parent.height - image.height*2/3) : parent.height
            visible: opacity > 0
            text: _targetPresent ? "" :
                //: Info label
                //% "NFC not supported"
                !NfcSystem.present ? qsTrId("yubikey-info-nfc_not_supported") :
                //: Hint label
                //% "Touch a YubiKey NFC"
                NfcSystem.enabled ? qsTrId("yubikey-info-touch_hint") :
                //: Info label
                //% "NFC is off"
                qsTrId("yubikey-info-nfc_disabled")

            Behavior on opacity { FadeAnimation { }}
        }
    }
}
