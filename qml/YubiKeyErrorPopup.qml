import QtQuick 2.0
import Sailfish.Silica 1.0

DockedPanel {
    id: thisItem

    property alias text: label.text

    width: parent.width
    height: content.height + _padding
    dock: Dock.Bottom
    background: null
    modal: true

    readonly property int _padding: Theme.paddingMedium

    Rectangle {
        id: content

        x: _padding
        width: parent.width - 2 * x
        height: Math.max(icon.height, label.height) + 2 * icon.y
        radius: ('topLeftCorner' in Screen) ? Screen.topLeftCorner.radius : Theme.paddingMedium
        color: Theme.overlayBackgroundColor
        border {
            color: Theme.secondaryHighlightColor
            width: Math.max(2, Math.floor(Theme.paddingSmall/3))
        }

        MouseArea {
            anchors.fill: parent
            onClicked: thisItem.open = false
        }

        Image {
            id: icon

            x: Theme.horizontalPageMargin
            y: Theme.paddingLarge
            source: "images/yubikey-error.svg"
            sourceSize.height: Theme.iconSizeLarge
        }

        Label {
            id: label

            anchors {
                top: icon.top
                left: icon.right
                leftMargin: Theme.paddingLarge
                right: parent.right
                rightMargin: Theme.horizontalPageMargin
            }
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: Theme.highlightColor
        }
    }
}
