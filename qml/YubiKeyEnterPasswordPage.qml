import QtQuick 2.0
import Sailfish.Silica 1.0

import "harbour"

Page {
    id: thisPage

    forwardNavigation: false

    property string yubiKeyId
    property bool changingPassword
    property Page destinationPage

    readonly property int _fullHeight: isPortrait ? Screen.height : Screen.width
    readonly property bool _canEnterPassword: passwordField.text.length > 0

    onStatusChanged: {
        switch (status) {
        case DialogStatus.Opening:
            passwordField.requestFocus()
            break
        case DialogStatus.Opened:
            forwardNavigation = false
            break
        }
    }

    // Otherwise width is changing with a delay, causing visible layout changes
    onIsLandscapeChanged: width = isLandscape ? Screen.height : Screen.width

    InfoLabel {
        id: promptLabel

        text:  changingPassword ?
            //: Input prompt (existing password is being replaced with a new one)
            //% "Enter new password for this YubiKey"
            qsTrId("yubikey-info-enter_password-change_prompt") :
            //: Input prompt (there was no password, creating one)
            //% "Enter password to set for this YubiKey"
            qsTrId("yubikey-info-enter_password-set_prompt")

        x: Theme.horizontalPageMargin
        width: parent.width - 2 * x
        anchors {
            bottom: panel.top
            bottomMargin: Theme.paddingLarge
        }

        // Hide it when it's only partially visible
        opacity: (y < Theme.paddingSmall) ? 0 : 1
        Behavior on opacity {
            enabled: !orientationTransitionRunning
            FadeAnimation { }
        }
    }

    Item {
        id: panel

        width: parent.width
        height: childrenRect.height
        y: Math.min((_fullHeight - height)/2, thisPage.height - panel.height - Theme.paddingLarge)

        HarbourPasswordInputField {
            id: passwordField

            anchors {
                left: panel.left
                top: parent.top
            }

            //: Placeholder for the new password field
            //% "New password"
            placeholderText: qsTrId("yubikey-enter_password-placeholder")
            //: Label for the new password field
            //% "New password"
            label: qsTrId("yubikey-enter_password-label")
            EnterKey.enabled: _canEnterPassword
            EnterKey.onClicked: panel.enterPassword()
        }

        Button {
            id: button

            anchors {
                topMargin: Theme.paddingLarge
                bottomMargin: 2 * Theme.paddingSmall
            }
            //: Button label (enter new password)
            //% "Enter"
            text: qsTrId("yubikey-enter_password-button")
            enabled: _canEnterPassword
            onClicked: panel.enterPassword()
        }

        function enterPassword() {
            pageStack.push(Qt.resolvedUrl("YubiKeyConfirmPasswordPage.qml"), {
                "yubiKeyId": yubiKeyId,
                "allowedOrientations": allowedOrientations,
                "destinationPage": destinationPage,
                "changingPassword": changingPassword,
                "password": passwordField.text
            })
        }
    }

    states: [
        State {
            name: "portrait"
            when: !isLandscape
            changes: [
                AnchorChanges {
                    target: passwordField
                    anchors.right: panel.right
                },
                PropertyChanges {
                    target: passwordField
                    anchors {
                        rightMargin: 0
                        bottomMargin: Theme.paddingLarge
                    }
                },
                AnchorChanges {
                    target: button
                    anchors {
                        top: passwordField.bottom
                        right: undefined
                        horizontalCenter: parent.horizontalCenter
                        bottom: undefined
                    }
                },
                PropertyChanges {
                    target: button
                    anchors.rightMargin: 0
                }
            ]
        },
        State {
            name: "landscape"
            when: isLandscape
            changes: [
                AnchorChanges {
                    target: passwordField
                    anchors.right: button.left
                },
                PropertyChanges {
                    target: passwordField
                    anchors {
                        rightMargin: Theme.horizontalPageMargin
                        bottomMargin: Theme.paddingSmall
                    }
                },
                AnchorChanges {
                    target: button
                    anchors {
                        top: undefined
                        right: panel.right
                        horizontalCenter: undefined
                        bottom: passwordField.bottom
                    }
                },
                PropertyChanges {
                    target: button
                    anchors.rightMargin: Theme.horizontalPageMargin
                }
            ]
        }
    ]
}
