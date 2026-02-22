import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

Dialog {
    id: thisDialog

    property string password
    property alias prompt: promptLabel.text

    canAccept: passwordField.text.length > 0 && passwordField.text === password

    readonly property int _fullHeight: isPortrait ? Screen.height : Screen.width
    readonly property real _landscapeWidth: Screen.height - (('topCutout' in Screen) ? Screen.topCutout.height : 0)

    onStatusChanged: {
        if (status === DialogStatus.Opening) {
            passwordField.requestFocus()
        }
    }

    // Otherwise width is changing with a delay, causing visible layout changes
    onIsLandscapeChanged: width = isLandscape ? _landscapeWidth : Screen.width

    DialogHeader {
        id: header

        //: Button label (confirm password)
        //% "Back"
        cancelText: qsTrId("yubikey-confirm_password-back-button")
        //: Button label (confirm password)
        //% "Confirm"
        acceptText: qsTrId("yubikey-confirm_password-accept-button")
        reserveExtraContent: true
        spacing: 0
    }

    DialogHeaderText {
        headerItem: header
        textItem: promptLabel
    }

    InfoLabel {
        id: promptLabel

        anchors {
            top: header.bottom
            topMargin: Theme.paddingLarge
            bottom: inputColumn.top
            bottomMargin: Theme.paddingLarge
        }

        verticalAlignment: Text.AlignVCenter

        // Hide it when it's only partially visible (and show the header label instead)
        opacity: (height >= contentHeight) ? 1 : 0
        visible: opacity > 0
    }

    Column {
        id: inputColumn

        readonly property int _ymax1: _screenHeight/2 - passwordField._backgroundRuleTopOffset - passwordField.y
        readonly property int _ymax2: thisDialog.height - passwordField.height - passwordField.y

        y: Math.min(_ymax1, _ymax2)
        width: parent.width

        HarbourPasswordInputField {
            id: passwordField

            readonly property int _backgroundRuleTopOffset: editContentItem ? (editContentItem.y + editContentItem.height) : 0

            width: parent.width
            //: Placeholder for the password confirmation prompt
            //% "New password again"
            placeholderText: qsTrId("yubikey-confirm_password-placeholder")
            label: text === password ?
                //: Label for the password confirmation prompt
                //% "Passwords match"
                qsTrId("yubikey-confirm_password-match-label") :
                //: Label for the password confirmation prompt
                //% "Passwords don't match"
                qsTrId("yubikey-confirm_password-mismatch-label")
            EnterKey.enabled: canAccept
            EnterKey.onClicked: accept()
        }
    }

    Image {
        id: infoImage

        x: Theme.horizontalPageMargin
        anchors.top: warning.top
        sourceSize.width: Theme.iconSizeSmall
        source: "images/info.svg"
    }

    Label {
        id: warning

        y: _fullHeight - contentHeight - Theme.paddingLarge
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            left: infoImage.right
            leftMargin: Theme.paddingLarge
        }

        //: Password confirmation description
        //% "Make sure you don't forget your password. It's impossible to either recover it or to access the tokens stored on YubiKey without knowing it. You will still be able to reset your YubiKey though, and start from scratch."
        text: qsTrId("yubikey-confirm_password-description")
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
        wrapMode: Text.Wrap
    }
}
