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

    property bool _initialized
    property real _flipAngle
    property real _unflipAngle

    onStatusChanged: {
        switch (status) {
        case PageStatus.Active:
            yubiKey.clear()
            if (!_initialized) {
                _initialized = true
                considerNextPageRequest.schedule()
            } else if (yubiKeyPage) {
                yubiKeyPage = null
            }
            break
        case PageStatus.Inactive:
            _unflip(true)
            break
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
                var method = nextPage ? _replacePage : _pushPage
                switch (yubiKey.authAccess) {
                case YubiKey.AccessNotActivated:
                    _createYubiKeyNotActivatedPage(method)
                    break
                case YubiKey.AccessDenied:
                    _createYubiAuthorizePage(method)
                    break
                case YubiKey.AccessOpen:
                case YubiKey.AccessGranted:
                    _createYubiKeyPage(method)
                    break
                }
            }
        }
    }

    function _createYubiKeyNotActivatedPage(createPage) {
        yubiKeyPage = null
        createPage(yubiKeyNotActivatedPageComponent)
    }

    function _createYubiAuthorizePage(createPage) {
        yubiKeyPage = null
        createPage(yubiKeyAuthorizePageComponent)
    }

    function _createYubiKeyPage(createPage) {
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

    function _pushPage(comp,prop) {
        return pageStack.push(comp, prop)
    }

    function _replacePage(comp,prop) {
        for (var p = pageStack.nextPage(thisPage); p; p = pageStack.nextPage(p)) {
            p.canNavigateForward = true
        }
        return pageStack.replaceAbove(thisPage, comp, prop)
    }

    function _unflip(immediate) {
        if (flipable.flipped) {
            var rotateX = isPortrait ? 0 : 1
            if (rotation.axis.x !== rotateX) {
                rotation.axis.x = rotateX
                rotation.axis.y = rotateX ? 0 : 1
                // This fixes a weird problem - after flipping, rotating and flipping again
                // the back panel gets rotated 180 degrees around z axis
                backPanelRotation.angle = isPortrait ? 180 : -180
            }
            rotation.angle = isPortrait ? 180 : -180
            _unflipAngle = isPortrait ? 360 : -360
            flipable.flipped = false
        }
        if (immediate) {
            flipAnimation.complete()
        }
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


    Flipable {
        id: flipable

        width: parent.width
        height: isPortrait ? Screen.height : Screen.width

        property bool flipped

        front: MainPageFront {
            id: frontPanel

            anchors.fill: parent
            onFlip: {
                backPanel.refresh()
                rotation.axis.x = isPortrait ? 0 : 1
                rotation.axis.y = isPortrait ? 1 : 0
                rotation.angle = 0
                backPanelRotation.angle = 0
                _flipAngle = isPortrait ? 180 : -180
                flipable.flipped = true
            }
        }

        back: MainPageBack {
            id: backPanel

            anchors.fill: parent
            transform: Rotation {
                id: backPanelRotation

                origin {
                    x: backPanel.width/2
                    y: backPanel.height/2
                }
                axis {
                    x: 0
                    y: 0
                    z: 1
                }
            }
            onFlip: {
                frontPanel.showSettingsButton()
                _unflip(false)
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
                when: !flipable.flipped

                PropertyChanges {
                    target: rotation
                    angle: _unflipAngle
                }
            },
            State {
                when: flipable.flipped

                PropertyChanges {
                    target: rotation
                    angle: _flipAngle
                }
            }
        ]

        transitions: Transition {
            NumberAnimation {
                id: flipAnimation

                target: rotation
                property: "angle"
                duration: 500
            }
        }
    }
}
