import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

Page {
    id: thisPage

    property alias text: label.text
    property bool yubiKeyPresent
    property alias extraContent: extraContentPlaceholder
    property alias busy: icon.busy
    property alias busyProgress: icon.busyProgress
    property alias progressValue: icon.progressValue

    signal activated()

    // Using full height for layout purposes, the actual page height may be
    // initially smaller if we are transitioning from the page which had VKB
    // open (which squeezes the page vertically)
    readonly property int _landscapeHeight: Screen.width
    readonly property int _portraitHeight: Screen.height
    property bool _popRequsted
    property bool _invalidYubiKey
    readonly property bool transitionInProgress: pageStack.currentPage &&
        (pageStack.currentPage.status === PageStatus.Activating ||
         pageStack.currentPage.status === PageStatus.Deactivating)

    function goBack()
    {
        // Avoid [D] doPop:492 - Warning: cannot pop while transition is in progress
        if (transitionInProgress) {
            popTimer.stop()
            _popRequsted = true
        } else {
            _popRequsted = false
            popTimer.start()
        }
    }

    function invalidYubiKeyConnected() {
        _invalidYubiKey = true
        if (yubiKeyPresent) {
            invalidYubiKeyPopup.show(false)
        }
    }

    onTransitionInProgressChanged: {
        if (!transitionInProgress && _popRequsted) {
            _popRequsted = false
            popTimer.start()
        }
    }

    onStatusChanged: {
        if (status === PageStatus.Active) {
            backNavigation = Qt.binding(function() { return !yubiKeyPresent })
            showNavigationIndicator = Qt.binding(function() { return backNavigation })
            thisPage.activated()
        }
    }

    onYubiKeyPresentChanged: {
        if (yubiKeyPresent) {
            // Typically invalidYubiKeyConnected comes before yubiKeyPresent
            // turns true, but let's handle the case if it happens the other
            // way around
            if (_invalidYubiKey) {
                invalidYubiKeyPopup.show(false)
            }
        } else {
            _invalidYubiKey = false
        }
    }

    Timer {
        id: popTimer

        interval: 0
        onTriggered: {
            backNavigation = true
            pageStack.pop(pageStack.previousPage(thisPage), PageStackAction.Animated)
        }
    }

    Item {
        id: iconContainer

        YubiKeyProgress {
            id: icon

            anchors.centerIn: parent
            busy: !yubiKeyPresent
            invertColors: false
            progressValue: 1
        }
    }

    Item {
        id: contentContainer

        anchors.right: parent.right

        InfoLabel {
            id: label

            width: parent.width - Theme.horizontalPageMargin - x
        }
    }

    // extraContentPlaceholder fills the space below the prompt (excluding some margins)
    Item {
        id: extraContentPlaceholder

        x: contentContainer.x + label.x
        y: contentContainer.y + label.y + label.height + Theme.paddingLarge
        width: label.width
        height: contentContainer.y + contentContainer.height - y - Theme.paddingLarge
    }

    YubiKeyPopup {
        id: invalidYubiKeyPopup

        //: Wait page popup (the touched YubiKey is not the one we are waiting for)
        //% "Wrong YubiKey"
        text: qsTrId("yubikey-popup-wrong_touch")
        iconSource: "images/yubikey-question.svg"
        isPortrait: thisPage.isPortrait
        autoHide: !yubiKeyPresent
    }

    states: [
        State {
            name: "portrait"
            when: isPortrait
            changes: [
                AnchorChanges {
                    target: contentContainer
                    anchors {
                        top: iconContainer.bottom
                        left: parent.left
                    }
                },
                AnchorChanges {
                    target: label
                    anchors {
                        top: parent.top
                        verticalCenter: undefined
                    }
                },
                PropertyChanges {
                    target: iconContainer
                    width: parent.width
                    height: icon.height + 2 * Theme.itemSizeLarge
                },
                PropertyChanges {
                    target: contentContainer
                    height: _portraitHeight - iconContainer.y - iconContainer.height
                },
                PropertyChanges {
                    target: label
                    horizontalAlignment: Text.AlignHCenter
                }
            ]
        },
        State {
            name: "landscape"
            when: !isPortrait
            changes: [
                AnchorChanges {
                    target: contentContainer
                    anchors {
                        top: parent.top
                        left: iconContainer.right
                    }
                },
                AnchorChanges {
                    target: label
                    anchors {
                        top: undefined
                        verticalCenter: parent.verticalCenter
                    }
                },
                PropertyChanges {
                    target: iconContainer
                    width: icon.width + 2 * Theme.itemSizeLarge
                    height: _landscapeHeight
                },
                PropertyChanges {
                    target: contentContainer
                    height: _landscapeHeight
                },
                PropertyChanges {
                    target: label
                    x: 0
                    horizontalAlignment: Text.AlignLeft
                }
            ]
        }
    ]
}
