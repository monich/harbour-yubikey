import QtQuick 2.0
import Sailfish.Silica 1.0

import "constants.js" as Constants

Item {
    id: thisItem

    anchors.fill: parent

    signal flip()

    function refresh() {
        settingsPanel.refresh()
    }

    Rectangle {
        id: panelBorder

        anchors {
            fill: parent
            margins: Theme.paddingMedium
        }
        radius: Constants.cornerRadius
        color: Theme.rgba(Theme.highlightBackgroundColor, 0.1)
        border {
            color: Theme.rgba(Theme.highlightColor, 0.4)
            width: Constants.thinBorder
        }

        Item {
            clip: true
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                bottom: button.top
                bottomMargin: 0
                margins: panelBorder.border.width
            }

            YubiKeySettingsPanel {
                id: settingsPanel

                anchors.fill: parent
            }
        }

        IconButton {
            id: button

            anchors {
                bottom: parent.bottom
                right: parent.right
                margins: Theme.paddingMedium + panelBorder.border.width
            }

            icon.source: "image://theme/icon-m-acknowledge"
            onClicked: thisItem.flip()
        }

        Label {
            anchors {
                horizontalCenter: parent.horizontalCenter
                bottom: parent.bottom
                bottomMargin: Theme.paddingMedium
            }
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.rgba(Theme.highlightColor, 0.4)
            //: Small description label (app version)
            //% "Version %1"
            text: qsTrId("yubikey-settings-version").arg("1.2.1")
        }
    }
}
