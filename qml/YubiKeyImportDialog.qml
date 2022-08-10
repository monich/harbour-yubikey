import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

import "harbour"

Dialog {
    id: thisDialog

    canAccept: model && model.haveSelectedTokens

    property alias model: list.model
    readonly property var selectedTokens: model.selectedTokens

    readonly property color _selectionBackground: Theme.rgba(Theme.highlightBackgroundColor, 0.1)

    SilicaListView {
        id: list
        anchors.fill: parent

        header: DialogHeader {
            //: Dialog button
            //% "Save"
            acceptText: qsTrId("yubikey-add_token-save")
            //: Dialog title
            //% "Select tokens"
            title: qsTrId("yubikey-select_dialog-title")
        }

        delegate: ListItem {
            id: delegate

            contentHeight: Theme.itemSizeMedium

            readonly property int itemIndex: model.index
            readonly property bool itemSelected: model.selected

            ListSeparator {
                anchors.top: parent.top
            }

            Rectangle {
                width: parent.width
                height: delegate.contentHeight
                color: (delegate.itemSelected && !delegate.highlighted) ? _selectionBackground : "transparent"
            }

            Column {
                spacing: Theme.paddingMedium
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: icon.left
                    rightMargin: Theme.paddingLarge
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    color: Theme.highlightColor
                    truncationMode: TruncationMode.Fade
                    font.bold: delegate.itemSelected
                    text: model.label
                }

                Label {
                    width: parent.width
                    color: Theme.secondaryHighlightColor
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    text: model.type === YubiKeyCard.TypeTOTP ? "TOTP" : "HOTP"
                }
            }

            HarbourHighlightIcon {
                id: icon

                source: model.selected ? "images/checked.svg" :  "images/unchecked.svg"
                sourceSize.width: Theme.iconSizeMedium
                highlightColor: delegate.down ? Theme.highlightColor : Theme.primaryColor
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
            }

            menu: Component {
                ContextMenu {
                    hasContent: delegate.itemSelected
                    MenuItem {
                        //: Generic menu item
                        //% "Edit"
                        text: qsTrId("yubikey-menu-edit")
                        onClicked: {
                            var token = list.model.getToken(delegate.itemIndex)
                            pageStack.push("YubiKeyTokenDialog.qml", {
                                "allowedOrientations": allowedOrientations,
                                "type": token.type,
                                "algorithm": token.algorithm,
                                "label": token.label,
                                "secret": token.secret,
                                "digits": token.digits,
                                "counter": token.counter
                            }).tokenAccepted.connect(delegate.applyChanges)
                        }
                    }
                }
            }

            onClicked: model.selected = !model.selected

            function applyChanges(dialog) {
                // Leave issuer as is
                list.model.setToken(itemIndex, dialog.type, dialog.algorithm,
                    dialog.label, model.issuer, dialog.secret, dialog.digits,
                    dialog.counter)
            }
        }

        footer: ListSeparator { }
    }
}
