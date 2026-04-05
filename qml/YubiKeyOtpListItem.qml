import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

ListItem {
    id: thisItem

    property int type
    property int transport
    property int entryOp
    property int entryOpState
    property string name
    property string newName
    property string password
    property bool steam
    property bool favorite
    property bool expired
    property bool landscape

    readonly property bool _usb: transport === YubiKey.TransportUSB
    readonly property bool _operationActive: entryOpState === YubiKeyOtpListModel.EntryOpStateActive
    readonly property bool _markedForRename: entryOp === YubiKeyOtpListModel.EntryOpRename
    readonly property bool _markedForRefresh: entryOp === YubiKeyOtpListModel.EntryOpRefresh
    readonly property bool _markedForDeletion: entryOp === YubiKeyOtpListModel.EntryOpDelete
    readonly property bool _markedForDeletionOrRename: _markedForDeletion || _markedForRename
    readonly property bool _marked: _markedForDeletionOrRename || _markedForRefresh
    readonly property bool _needPhysicalTouch: _markedForRefresh && _usb && _operationActive

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

        HarbourMarqueeText {
            width: parent.width
            color: favorite ? Theme.secondaryColor : Theme.secondaryHighlightColor
            font {
                pixelSize: Theme.fontSizeExtraSmall
                bold: favorite
            }
            speed: 0.5
            autoStartDelay: 2000
            opacity: (_markedForDeletionOrRename || (_markedForRefresh && _usb)) ? 0.4 : 1
            text: _needPhysicalTouch ?
                //: List item text (for USB key)
                //% "Touch YubiKey button to refresh"
                qsTrId("yubikey-item-touch_to_refresh") :
                _markedForDeletion ?
                //: List item text
                //% "Tap YubiKey to delete this token"
                qsTrId("yubikey-item-tap_to_delete") :
                _markedForRename ?
                //: List item text
                //% "Tap YubiKey to rename this token"
                qsTrId("yubikey-item-tap_to_rename") :
                _markedForRefresh ?
                //: List item text
                //% "Tap YubiKey to refresh"
                qsTrId("yubikey-item-tap_to_refresh") :
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

        Item {
            width: actionButton.width
            height: width
            anchors.verticalCenter: parent.verticalCenter

            Loader {
                active: opacity > 0
                opacity: _needPhysicalTouch ? 1 : 0
                anchors.centerIn: actionButton
                sourceComponent: Component {
                    YubiKeyTouchIcon {
                        blinking: true
                        width: Theme.itemSizeSmall - 2 * Theme.paddingMedium
                        height: width
                    }
                }
            }

            IconButton {
                id: actionButton

                anchors.centerIn: parent
                highlighted: down || thisItem.down
                visible: (_markedForDeletionOrRename || _refreshable) && ! _needPhysicalTouch
                opacity: menuOpen ? 0.4 : 1
                icon.source: _marked ? "image://theme/icon-m-clear" : "image://theme/icon-m-refresh"
                onClicked: _marked ? thisItem.cancel() : thisItem.requestRefresh()
            }
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
