import QtQuick 2.0
import Sailfish.Silica 1.0

import "harbour"

Item {
    id: thisItem

    property bool isLandscape
    property bool invalidPassword
    property bool yubiKeyPresent
    property real contentHeight
    property alias password: passwordField.text
    property alias savePassword: savePasswordSwitch.checked

    readonly property bool _canEnterPassword: passwordField.text.length > 0

    signal enterPassword()

    function _enterPassword() {
        enterPassword(passwordField.text)
        passwordField.text = ""
    }

    Component.onCompleted: passwordField.requestFocus()

    HarbourHighlightIcon {
        source: "images/yubikey-lock.svg"
        width: Theme.itemSizeHuge
        y: Math.round((infoLabel.y - height)/2)
        sourceSize.width: width
        anchors.horizontalCenter: parent.horizontalCenter
        highlightColor: yubiKeyPresent ? Theme.primaryColor : Theme.highlightColor
        visible: opacity > 0
        // Hide it also when it's getting too close to the top if the view
        opacity: (isLandscape || y < Theme.paddingLarge) ? 0 : 1
    }

    InfoLabel {
        id: infoLabel

        anchors {
            bottom: passwordInput.top
            bottomMargin: Theme.paddingLarge
        }
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * x
        visible: y >= Theme.paddingLarge
        text: invalidPassword ?
            //: Info label
            //% "Wrong YubiKey password, please try again"
            qsTrId("yubikey-info-wrong_password") :
            //: Info label
            //% "YubiKey password"
            qsTrId("yubikey-info-password_required")
    }

    Column {
        id: passwordInput

        y: Math.min(thisItem.height/2 - passwordField.height, maxY)
        anchors.left: parent.left

        property real maxY

        HarbourPasswordInputField {
            id: passwordField

            width: parent.width
            EnterKey.enabled: _canEnterPassword
            EnterKey.onClicked: thisItem._enterPassword()
        }

        TextSwitch {
            id: savePasswordSwitch

            width: parent.width

            //: Switch label
            //% "Remember password"
            text: qsTrId("yubikey-remember_password-text")
            //: Switch label
            //% "The password is stored in a hashed form from which the original password cannot be recovered. However, this hashed form can still be copied and used for accessing your key even without knowing the original password. By choosing to store the password, you accept that risk."
            description: qsTrId("yubikey-remember_password-description")
        }
    }

    Button {
        id: enterPasswordButton

        anchors {
            top: passwordInput.bottom
            horizontalCenter: parent.horizontalCenter
            rightMargin: Theme.horizontalPageMargin
        }
        //: Button label
        //% "Enter"
        text: qsTrId("yubikey-button-enter_password")
        enabled: _canEnterPassword
        onClicked: thisItem._enterPassword()
    }

    states: [
        State {
            name: "portrait"
            when: !isLandscape
            changes: [
                AnchorChanges {
                    target: passwordInput
                    anchors.right: parent.right
                },
                AnchorChanges {
                    target: enterPasswordButton
                    anchors {
                        top: passwordInput.bottom
                        right: undefined
                        horizontalCenter: parent.horizontalCenter
                    }
                },
                PropertyChanges {
                    target: passwordInput
                    maxY: contentHeight - passwordInput.height - enterPasswordButton.height - 2 * Theme.paddingLarge
                },
                PropertyChanges {
                    target: enterPasswordButton
                    anchors.topMargin: Theme.paddingLarge
                }
            ]
        },
        State {
            name: "landscape"
            when: isLandscape
            changes: [
                AnchorChanges {
                    target: passwordInput
                    anchors.right: enterPasswordButton.left
                },
                AnchorChanges {
                    target: enterPasswordButton
                    anchors {
                        top: passwordInput.top
                        right: parent.right
                        horizontalCenter: undefined
                    }
                },
                PropertyChanges {
                    target: passwordInput
                    maxY: contentHeight - passwordField.height
                },
                PropertyChanges {
                    target: enterPasswordButton
                    anchors.topMargin: 0
                }
            ]
        }
    ]
}
