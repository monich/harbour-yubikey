import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    property alias title: titleLabel.text
    property alias text: warningLabel.text

    DialogHeader {
        id: header

        spacing: 0
    }

    SilicaFlickable {
        // Same space above and below the content
        contentHeight: column.height + 2 * column.y
        clip: true
        anchors {
            top: header.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }

        Column {
            id: column

            y: spacing
            width: parent.width
            spacing: Theme.paddingLarge

            InfoLabel {
                id: titleLabel

                font.bold: true
            }

            Label {
                id: warningLabel

                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                wrapMode: Text.Wrap
                color: Theme.highlightColor
            }
        }

        VerticalScrollDecorator { }
    }
}
