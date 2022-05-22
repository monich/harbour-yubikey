import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

Page {
    id: thisPage

    forwardNavigation: false

    property alias yubiKeyId: yubiKey.yubiKeyId
    property bool changingPassword
    property string password
    property Page destinationPage

    property bool _wrongPassword
    readonly property int _fullHeight: isPortrait ? Screen.height : Screen.width
    readonly property bool _canCheckPassword: passwordField.text.length > 0 && !_wrongPassword

    function done() {
        pageStack.pop(destinationPage);
    }

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

    YubiKeyCard {
        id: yubiKey
    }

    InfoLabel {
        id: promptLabel

        text: changingPassword ?
            //: Input prompt
            //% "Please type in your new YubiKey password one more time"
            qsTrId("yubikey-confirm_password-prompt-change") :
            //: Input prompt
            //% "Please type in your YubiKey password one more time"
            qsTrId("yubikey-confirm_password-prompt-set")

        // Bind to panel x position for shake animation
        x: Theme.horizontalPageMargin + panel.x
        width: parent.width - 2 * Theme.horizontalPageMargin
        anchors {
            bottom: warning.top
            bottomMargin: Theme.paddingLarge
        }

        // Hide it when it's only partially visible
        opacity: (y < Theme.paddingSmall) ? 0 : 1
        Behavior on opacity {
            enabled: !orientationTransitionRunning
            FadeAnimation { }
        }
    }

    Label {
        id: warning

        // Bind to panel x position for shake animation
        x: Theme.horizontalPageMargin + panel.x
        width: parent.width - 2 * Theme.horizontalPageMargin
        anchors {
            bottom: panel.top
            bottomMargin: Theme.paddingLarge
        }

        //: Password confirmation description
        //% "Make sure you don't forget your password. It's impossible to either recover it or to access the tokens stored on YubiKey without knowing it. You will still be able to reset your YubiKey though, and start from scratch."
        text: qsTrId("yubikey-confirm_password-description")
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
        wrapMode: Text.Wrap

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

            //: Placeholder for the password confirmation prompt
            //% "New password again"
            placeholderText: qsTrId("yubikey-confirm_password-placeholder")
            //: Label for the password confirmation prompt
            //% "New password"
            label: qsTrId("yubikey-confirm_password-label")
            onTextChanged: _wrongPassword = false
            EnterKey.enabled: _canCheckPassword
            EnterKey.onClicked: panel.checkPassword()
        }

        Button {
            id: button

            anchors {
                topMargin: Theme.paddingLarge
                bottomMargin: 2 * Theme.paddingSmall
            }
            //: Button label (confirm password)
            //% "Confirm"
            text: qsTrId("yubikey-confirm_password-button")
            enabled: _canCheckPassword
            onClicked: panel.checkPassword()
        }

        function checkPassword() {
            if (passwordField.text === password) {
                passwordField.text = ""
                forwardNavigation = true
                if (yubiKey.setPassword(password)) {
                    thisPage.done()
                } else {
                    var waitPage = pageStack.push("YubiKeyWaitPage.qml", {
                        "yubiKeyId": yubiKeyId,
                        "allowedOrientations": allowedOrientations,
                        "text": changingPassword ?
                            //: Status label
                            //% "Touch the same YubiKey to change the password"
                            qsTrId("yubikey-wait-change_password") :
                            //: Status label
                            //% "Touch the same YubiKey to set the password"
                            qsTrId("yubikey-wait-set_password"),
                    })
                    waitPage.waitDone.connect(function() {
                        if (yubiKey.setPassword(password)) {
                            thisPage.done();
                        } else {
                            waitPage.tryAgain()
                        }
                    })
                }
            } else {
                _wrongPassword = true
                wrongPasswordAnimation.start()
                passwordField.requestFocus()
            }
        }
    }

    HarbourShakeAnimation  {
        id: wrongPasswordAnimation

        target: panel
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
