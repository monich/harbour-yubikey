import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

Dialog {
    id: thisDialog

    property var yubiKey

    canAccept: passwordField.text.length > 0
    acceptDestination: validatePasswordComponent

    property bool _invalidPassword
    property bool _yubiKeyPresent: yubiKey.present
    readonly property int _screenHeight: isLandscape ? Screen.width : Screen.height
    readonly property bool _darkOnLight : "colorScheme" in Theme && Theme.colorScheme === Theme.DarkOnLight
    readonly property real _landscapeWidth: Screen.height - (('topCutout' in Screen) ? Screen.topCutout.height : 0)

    function isOk() {
        switch (yubiKey.authAccess) {
        case YubiKey.AccessUnknown:
        case YubiKey.AccessDenied:
            return true
        }
        return false
    }

    // Otherwise width is changing with a delay, causing unpleasant layout effects
    // when the on-screen keyboard is active and taking part of the screen.
    onIsLandscapeChanged: width = isLandscape ? _landscapeWidth : Screen.width

    onStatusChanged: {
        if (status === DialogStatus.Opened) {
            backNavigation = Qt.binding(function() { return !_yubiKeyPresent })
            passwordField.requestFocus()
        }
    }

    onAccepted: yubiKey.authorize(passwordField.text, savePasswordSwitch.checked)

    Component {
        id: validatePasswordComponent

        YubiKeyWaitPage {
            id: validatePasswordPage

            allowedOrientations: thisDialog.allowedOrientations
            yubiKeyPresent: _yubiKeyPresent
            text: _yubiKeyPresent ?
                //: Status label
                //% "Validating the password"
                qsTrId("yubikey-status-validating_password")  :
                //: Status label
                //% "Touch the same YubiKey to validate the password"
                qsTrId("yubikey-status-waiting_to_authorize")

            Connections {
                target: thisDialog.yubiKey
                onYubiKeyValidationFailed: {
                    // Go back to the password entry page
                    _invalidPassword = true
                    validatePasswordPage.goBack()
                }
            }
        }
    }

    DialogHeader {
        id: header

        reserveExtraContent: true
        spacing: 0
    }

    DialogHeaderText {
        headerItem: header
        textItem: infoLabel
    }

    Item {
        id: iconContainer

        anchors.top: header.bottom

        Image {
            id: icon

            readonly property int size: Math.min(parent.height - 2 * Theme.paddingLarge, Theme.iconSizeExtraLarge)

            anchors.centerIn: parent
            sourceSize.height: size
            source: "images/yubikey-lock.svg"
            opacity: size >= Theme.iconSizeMedium ? 1 : 0
        }
    }

    InfoLabel {
        id: infoLabel

        visible: opacity > 0
        opacity: (y >= header.height + Theme.paddingSmall) ? 1 : 0
        anchors {
            left: inputContainer.left
            leftMargin: Theme.horizontalPageMargin
            right: inputContainer.right
            rightMargin: Theme.horizontalPageMargin
            bottom: inputContainer.top
            bottomMargin: Theme.paddingLarge
        }
        text: _invalidPassword ?
            //: Info label
            //% "Wrong YubiKey password, please try again"
            qsTrId("yubikey-info-wrong_password") :
            //: Info label
            //% "YubiKey password"
            qsTrId("yubikey-info-password_required")
    }

    Column {
        id: inputContainer

        readonly property int _ymax1: _screenHeight/2 - passwordField._backgroundRuleTopOffset - passwordField.y
        readonly property int _ymax2: thisDialog.height - passwordField.height - passwordField.y

        y: Math.min(_ymax1, _ymax2)
        anchors.right: parent.right

        HarbourPasswordInputField {
            id: passwordField

            readonly property int _backgroundRuleTopOffset: editContentItem ? (editContentItem.y + editContentItem.height) : 0
            readonly property int _backgroundRuleY: y + _backgroundRuleTopOffset

            width: parent.width
            EnterKey.enabled: canAccept
            EnterKey.onClicked: accept()
        }

        TextSwitch {
            id: savePasswordSwitch

            //: Switch label
            //% "Remember password"
            text: qsTrId("yubikey-remember_password-text")
            //: Switch description
            //% "The password is stored in a hashed form from which the original password cannot be recovered. However, this hashed form can still be copied and used for accessing your key even without knowing the original password. By choosing to store the password, you accept that risk."
            description: qsTrId("yubikey-remember_password-description")
        }
    }

    states: [
        State {
            name: "portrait"
            when: isPortrait
            changes: [
                AnchorChanges {
                    target: iconContainer
                    anchors.bottom: infoLabel.top
                },
                PropertyChanges {
                    target: iconContainer
                    width: thisDialog.width
                    anchors.bottomMargin: Theme.paddingLarge
                },
                AnchorChanges {
                    target: inputContainer
                    anchors.left: parent.left
                }
            ]
        },
        State {
            name: "landscape"
            when: !isPortrait
            changes: [
                AnchorChanges {
                    target: iconContainer
                    anchors.bottom: inputContainer.top
                },
                PropertyChanges {
                    target: iconContainer
                    width: 2 * Theme.iconSizeExtraLarge
                    anchors.bottomMargin: - passwordField._backgroundRuleY - Theme.paddingLarge
                },
                AnchorChanges {
                    target: inputContainer
                    anchors.left: iconContainer.right
                }
            ]
        }
    ]
}
