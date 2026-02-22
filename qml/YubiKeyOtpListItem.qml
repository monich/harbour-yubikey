import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

ListItem {
    id: thisItem

    property int type
    property int entryOp
    property string name
    property string newName
    property string password
    property bool steam
    property bool favorite
    property bool expired
    property bool landscape

    readonly property bool _markedForRefresh: entryOp === YubiKeyOtpListModel.EntryOpRefresh
    readonly property bool _markedForRename: entryOp === YubiKeyOtpListModel.EntryOpRename
    readonly property bool _markedForDeletion: entryOp === YubiKeyOtpListModel.EntryOpDelete
    readonly property bool _markedForDeletionOrRename: _markedForDeletion || _markedForRename
    readonly property bool _operationPending: _markedForDeletionOrRename || _markedForRefresh


    contentHeight: Theme.itemSizeMedium

    signal cancel()
    signal requestRefresh()

    readonly property bool _refreshable: type === YubiKey.TypeHOTP

    onPasswordChanged: {
        if (_refreshable) {
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
        spacing: Theme.paddingMedium
        anchors {
            left: parent.left
            leftMargin: thisItem.landscape ? Theme.paddingLarge : Theme.horizontalPageMargin
            right: rightArea.left
            rightMargin: _refreshable ? 0 : Theme.paddingLarge
            verticalCenter: parent.verticalCenter
        }

        Label {
            id: nameLabel

            width: parent.width
            color: (favorite && !_markedForDeletionOrRename) ? Theme.primaryColor : Theme.highlightColor
            font {
                bold: favorite && !_markedForDeletionOrRename
                strikeout: _markedForDeletion
            }
            truncationMode: TruncationMode.Fade
            text: _markedForRename ? newName : name
            opacity: _markedForDeletionOrRename ? 0.4 : 1
        }

        Label {
            width: parent.width
            color: favorite ? Theme.secondaryColor : Theme.secondaryHighlightColor
            font {
                pixelSize: Theme.fontSizeExtraSmall
                bold: favorite
            }
            truncationMode: TruncationMode.Fade
            opacity: _markedForDeletionOrRename ? 0.4 : 1
            //: List item text
            //% "Touch YubiKey to delete this token"
            text: _markedForDeletion ? qsTrId("yubikey-item-touch_to_delete") :
                //: List item text
                //% "Touch YubiKey to rename this token"
                _markedForRename ? qsTrId("yubikey-item-touch_to_rename")  :
                steam ? "Steam" :
                type === YubiKey.TypeTOTP ? "TOTP" :
                type === YubiKey.TypeHOTP ? "HOTP" :
                ""
        }
    }

    Row {
        id: rightArea

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
            visible: _markedForDeletionOrRename || _refreshable
            opacity: menuOpen ? 0.4 : 1
            icon.source: _operationPending ? "image://theme/icon-m-clear" : "image://theme/icon-m-refresh"
            onClicked: _operationPending ? thisItem.cancel() : thisItem.requestRefresh()
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
            visible: !_markedForDeletionOrRename && opacity > 0
            opacity: (expired || _markedForRefresh) ? 0.4 : 1
            transform: HarbourTextFlip {
                text: thisItem.password
                target: passwordLabel
            }

            Behavior on opacity { FadeAnimation { duration: 250 } }
        }
    }

    ListSeparator {
        anchors.bottom: parent.bottom
    }
}
