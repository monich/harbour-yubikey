import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

SilicaFlickable {
    id: thisItem

    contentHeight: content.height + 2 * content.y

    function refresh() {
        authDataModel.refresh()
    }

    YubiKeyAuthDataModel {
        id: authDataModel
    }

    Column {
        id: content

        y: Theme.paddingLarge
        width: parent.width

        Label {
            color: Theme.highlightColor
            anchors {
                right: parent.right
                rightMargin: Theme.horizontalPageMargin
            }
            font {
                pixelSize: Theme.fontSizeLarge
                family: Theme.fontFamilyHeading
            }
            //: Page header
            //% "Settings"
            text: qsTrId("yubikey-settings-header")
        }

        SectionHeader {
            //: Settings section header
            //% "Saved YubiKey passwords"
            text: qsTrId("yubikey-settings-section_saved_passwords")
        }

        Rectangle {
            id: authDataViewContainer

            readonly property int _borderWidth: Math.max(2, Math.floor(Theme.paddingSmall/3))
            readonly property color _borderColor: Theme.rgba(Theme.highlightColor, 0.4)

            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            height: authDataView.height + 2 * _borderWidth
            color: "transparent"
            visible: authDataView.count > 0
            border {
                color: _borderColor
                width: _borderWidth
            }

            SilicaListView {
                id: authDataView

                x: authDataViewContainer._borderWidth
                y: authDataViewContainer._borderWidth
                width: parent.width - 2 * x
                height: contentHeight
                model: authDataModel

                header: Rectangle {
                    width: authDataView.width
                    height: yubiKeyIdHeaderLabel.height + 2 * Theme.paddingSmall
                    color: Theme.rgba(Theme.primaryColor, 0.05)

                    Label {
                        id: yubiKeyIdHeaderLabel

                        anchors {
                            left: parent.left
                            leftMargin: Theme.paddingMedium
                            right: lastAccessHeaderLabel.left
                            rightMargin: Theme.paddingMedium
                            verticalCenter: parent.verticalCenter
                        }
                        color: Theme.secondaryHighlightColor
                        truncationMode: TruncationMode.Fade
                        //: List header
                        //% "YubiKey"
                        text: qsTrId("yubikey-settings-saved_passwords_header-yubikey_id")
                    }

                    Label {
                        id: lastAccessHeaderLabel

                        anchors {
                            right: parent.right
                            rightMargin: yubiKeyIdHeaderLabel.anchors.leftMargin
                            verticalCenter: parent.verticalCenter
                        }
                        color: yubiKeyIdHeaderLabel.color
                        horizontalAlignment: Text.AlignRight
                        //: List header
                        //% "Last access"
                        text: qsTrId("yubikey-settings-saved_passwords_header-last_access")
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        x: authDataViewContainer._borderWidth
                        width: parent.width - 2 * x
                        height: authDataViewContainer._borderWidth
                        color: authDataViewContainer._borderColor
                    }
                }

                delegate: YubiKeyAuthDataItem {
                    yubiKeyId: model.yubiKeyId
                    lastAccess: model.lastAccess
                    menu: ContextMenu {
                        onHeightChanged: {
                            // Make sure we are inside the screen area
                            var flickable = thisItem
                            var bottom = parent.mapToItem(flickable, x, y).y + height
                            if (bottom > flickable.height) {
                                flickable.contentY += bottom - flickable.height
                            }
                        }

                        MenuItem {
                            id: deleteMenuItem

                            //: Generic menu item
                            //% "Delete"
                            text: qsTrId("yubikey-menu-delete")
                            onClicked: {
                                var removeId = model.yubiKeyId
                                //: Remorse popup text
                                //% "Deleting"
                                remorseAction(qsTrId("yubikey-menu-delete_remorse"), function() {
                                    authDataModel.remove(removeId)
                                })
                            }
                        }
                    }
                }
            }
        }

        Label {
            // Repeat the text from the authorization dialog
            text: qsTrId("yubikey-remember_password-description")
            x: authDataViewContainer.x
            width: authDataViewContainer.width
            visible: authDataViewContainer.visible
            topPadding: Theme.paddingMedium
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
            wrapMode: Text.Wrap
        }

        InfoLabel {
            //: Info label
            //% "No YubiKey passwords stored on this device"
            text: qsTrId("yubikey-settings-saved_passwords-none")
            visible: !authDataViewContainer.visible
        }
    }

    VerticalScrollDecorator { }
}
