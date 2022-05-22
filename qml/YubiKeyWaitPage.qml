import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

Page {
    id: thisPage

    property alias yubiKeyId: yubiKey.yubiKeyId
    property alias text: label.text
    property var complete
    property Page destinationPage

    backNavigation: !NfcAdapter.targetPresent

    signal accessKeyNotAccepted()
    signal waitDone(var keyState, var waitId, var success)

    property bool _done
    property int _waitId

    function tryAgain() {
        _done = false
    }

    function waitForId(waitId) {
        _done = false
        _waitId = waitId
    }

    YubiKeyCard {
        id: yubiKey

        onAccessKeyNotAccepted: thisPage.accessKeyNotAccepted()
        onYubiKeyStateChanged: {
            if (!_done && !_waitId && (yubiKeyState === YubiKeyCard.YubiKeyStateReady ||
                yubiKeyState === YubiKeyCard.YubiKeyStateUnauthorized)) {
                done(false)
            }
        }
        onOperationIdsChanged: {
            if (!_done && _waitId && !validOperationId(_waitId)) {
                done(false)
            }
        }
        onOperationFinished: {
            if (!_done && _waitId === operationId) {
                done(success)
            }
        }

        function done(success) {
            var waitId = _waitId
            _waitId = 0
            _done = true
            thisPage.waitDone(yubiKeyState, waitId, success)
            if (complete) {
                complete(thisPage, yubiKeyState, waitId, success)
            }
            if (_done && status === PageStatus.Active) {
                pageStack.pop(destinationPage)
            }
        }
    }

    Item {
        id: iconContainer

        YubiKeyProgress {
            id: icon

            anchors.centerIn: parent
            busy: true
        }
    }

    Item {
        id: labelContainer

        anchors.right: parent.right

        InfoLabel {
            id: label

            anchors.verticalCenter: parent.verticalCenter
        }
    }

    states: [
        State {
            name: "portrait"
            when: isPortrait
            changes: [
                PropertyChanges {
                    target: iconContainer
                    width: parent.width
                    height: icon.height + 2 * Theme.itemSizeLarge
                },
                AnchorChanges {
                    target: labelContainer
                    anchors.left: parent.left
                },
                PropertyChanges {
                    target: labelContainer
                    y: iconContainer.y + iconContainer.height
                    height: Screen.height - y
                },
                PropertyChanges {
                    target: label
                    anchors.verticalCenterOffset: - label.height
                }
            ]
        },
        State {
            name: "landscape"
            when: !isPortrait
            changes: [
                PropertyChanges {
                    target: iconContainer
                    width: icon.width + 2 * Theme.itemSizeLarge
                    height: Screen.width
                },
                AnchorChanges {
                    target: labelContainer
                    anchors.left: iconContainer.right
                },
                PropertyChanges {
                    target: labelContainer
                    y: 0
                    height: Screen.width
                },
                PropertyChanges {
                    target: label
                    anchors.verticalCenterOffset: 0
                }
            ]
        }
    ]
}
