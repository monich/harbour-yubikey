import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

ListItem {
    id: thisItem

    property int type
    property alias name: nameLabel.text
    property string password
    property bool favorite
    property bool landscape
    property bool totpValid
    property bool markedForRefresh
    property bool markedForDeletion

    contentHeight: Theme.itemSizeMedium

    signal cancel()
    signal requestRefresh()


    onPasswordChanged: {
        if (type === YubiKeyCard.TypeHOTP) {
            actionButtonAnimation.start()
        }
    }

    NumberAnimation {
        id: actionButtonAnimation

        target: actionButton
        duration: 500
        alwaysRunToEnd: true
        easing.type: Easing.InOutQuad
        property: "rotation"
        from: 0
        to: 360
    }

    Column {
        visible: !markedForDeletion
        spacing: Theme.paddingMedium
        anchors {
            left: parent.left
            leftMargin: thisItem.landscape ? Theme.paddingLarge : Theme.horizontalPageMargin
            right: rightArea.left
            rightMargin: (type === YubiKeyCard.TypeHOTP) ? 0 : Theme.paddingLarge
            verticalCenter: parent.verticalCenter
        }

        Label {
            id: nameLabel

            width: parent.width
            color: favorite ? Theme.primaryColor : Theme.highlightColor
            font.bold: favorite
            truncationMode: TruncationMode.Fade
        }

        Label {
            width: parent.width
            color: favorite ? Theme.secondaryColor : Theme.secondaryHighlightColor
            font {
                pixelSize: Theme.fontSizeExtraSmall
                bold: favorite
            }
            truncationMode: TruncationMode.Fade
            text: type === YubiKeyCard.TypeTOTP ? "TOTP" : "HOTP"
        }
    }

    Row {
        id: rightArea

        visible: !markedForDeletion
        height: parent.height
        spacing: isLandscape ? Theme.paddingLarge : 0
        anchors {
            right: parent.right
            rightMargin: thisItem.landscape ? Theme.paddingLarge : Theme.horizontalPageMargin
        }

        IconButton {
            id: actionButton

            anchors.verticalCenter: parent.verticalCenter
            highlighted: down || thisItem.down
            visible: type === YubiKeyCard.TypeHOTP
            opacity: menuOpen ? 0.4 : 1
            icon.source: markedForRefresh ? "image://theme/icon-m-clear" : "image://theme/icon-m-refresh"
            onClicked: markedForRefresh ? thisItem.cancel() : thisItem.requestRefresh()
        }

        Label {
            id: passwordLabel

            color: thisItem.down ? Theme.highlightColor : Theme.primaryColor
            anchors.verticalCenter: parent.verticalCenter
            font {
                pixelSize: Theme.fontSizeLarge
                family: Theme.fontFamilyHeading
                bold: true
            }
            visible: opacity > 0
            opacity: ((type === YubiKeyCard.TypeHOTP && !markedForRefresh) ||
                (type === YubiKeyCard.TypeTOTP && totpValid)) ? 1 : 0.4
            transform: HarbourTextFlip {
                text: thisItem.password
                target: passwordLabel
            }

            Behavior on opacity { FadeAnimation { duration: 250 } }
        }
    }

    Label {
        anchors {
            verticalCenter: parent.verticalCenter
            left: parent.left
            leftMargin: Theme.horizontalPageMargin
            right: clearButton.left
            rightMargin: Theme.paddingLarge
        }
        visible: markedForDeletion
        color: thisItem.down ? Theme.secondaryHighlightColor : Theme.secondaryColor
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.Wrap
        opacity: 0.4
        //: List item text
        //% "Touch YubiKey to delete this token"
        text: qsTrId("yubikey-item-touch_to_delete")
    }

    IconButton {
        id: clearButton

        anchors {
            verticalCenter: parent.verticalCenter
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
        }
        highlighted: down || thisItem.down
        visible: markedForDeletion
        opacity: menuOpen ? 0.4 : 1
        icon.source: "image://theme/icon-m-clear"
        onClicked: thisItem.cancel()
    }

    ListSeparator {
        anchors.bottom: parent.bottom
    }
}
