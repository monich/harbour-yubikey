import QtQuick 2.0
import Sailfish.Silica 1.0
import org.nemomobile.notifications 1.0
import harbour.yubikey 1.0

Page {
    id: thisPage

    backNavigation: false
    showNavigationIndicator: false

    property var yubiKey
    property Page yubiKeyPage

    readonly property bool _targetPresent: NfcAdapter.targetPresent
    property bool _initialized

    onStatusChanged: {
        if (status === PageStatus.Active) {
            yubiKey.clear()
            if (!_initialized) {
                _initialized = true
                considerNextPageRequest.schedule()
            } else if (yubiKeyPage) {
                yubiKeyPage = null
            }
        }
    }

    Connections {
        target: yubiKey
        onYubiKeyIdChanged: considerNextPageRequest.schedule()
        onAuthAccessChanged: considerNextPageRequest.schedule()
        onRestrictedYubiKeyConnected: considerNextPageRequest.schedule()
        onYubiKeyConnected: considerNextPageRequest.schedule()
        onYubiKeyValidationFailed: considerNextPageRequest.schedule()
    }

    PageTransitionRequest {
        id: considerNextPageRequest

        onExecute: {
            // The main page doesn't have any attached pages, the next page must
            // be above it (if there's any). The page above must provide isOk()
            // function returning true if it's ok with the current YubiKey state
            var nextPage = pageStack.nextPage(thisPage)
            if (!nextPage || !nextPage.isOk()) {
                var method = nextPage ? replacePage : pushPage
                switch (yubiKey.authAccess) {
                case YubiKey.AccessNotActivated:
                    createYubiKeyNotActivatedPage(method)
                    break
                case YubiKey.AccessDenied:
                    createYubiAuthorizePage(method)
                    break
                case YubiKey.AccessOpen:
                case YubiKey.AccessGranted:
                    createYubiKeyPage(method)
                    break
                }
            }
        }
    }

    function createYubiKeyNotActivatedPage(createPage) {
        yubiKeyPage = null
        createPage(yubiKeyNotActivatedPageComponent)
    }

    function createYubiAuthorizePage(createPage) {
        yubiKeyPage = null
        createPage(yubiKeyAuthorizePageComponent)
    }

    function createYubiKeyPage(createPage) {
        // Make a copy of these 3 fields.
        // They never as long as we're dealing with the same YubiKey.
        var yubiKeyId = yubiKey.yubiKeyId
        var yubiKeySerial = yubiKey.yubiKeySerial
        var yubiKeyFirmware = yubiKey.yubiKeyVersionString
        yubiKeyPage = createPage(yubiKeyPageComponent, {
            "yubiKeyId": yubiKeyId,
            "yubiKeySerial": yubiKeySerial,
            "yubiKeyFirmware" : yubiKeyFirmware
        })
    }

    function pushPage(comp,prop) {
        return pageStack.push(comp, prop)
    }

    function replacePage(comp,prop) {
        for (var p = pageStack.nextPage(thisPage); p; p = pageStack.nextPage(p)) {
            p.canNavigateForward = true
        }
        return pageStack.replaceAbove(thisPage, comp, prop)
    }

    Component {
        id: yubiKeyNotActivatedPageComponent

        YubiKeyNotActivatedPage {
            allowedOrientations: thisPage.allowedOrientations
            yubiKey: thisPage.yubiKey
        }
    }

    Component {
        id: yubiKeyAuthorizePageComponent

        YubiKeyAuthorizeDialog {
            allowedOrientations: thisPage.allowedOrientations
            yubiKey: thisPage.yubiKey
        }
    }

    Component {
        id: yubiKeyPageComponent

        YubiKeyPage {
            allowedOrientations: thisPage.allowedOrientations
            yubiKey: thisPage.yubiKey
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
