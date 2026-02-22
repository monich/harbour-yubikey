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

            Row {
                // Center the title
                x: Math.max(Math.floor((parent.width - titleLabel.width)/2 - titleLabel.x), 0)
                spacing: Theme.paddingLarge

                Image {
                    source: "images/warning.svg"
                    sourceSize.height: Theme.iconSizeSmall
                    anchors.bottom: titleLabel.baseline
                }

                Label {
                    id: titleLabel

                    //: Warning dialog title
                    //% "Warning"
                    text: qsTrId("yubikey-warning-title")
                    color: Theme.secondaryHighlightColor
                    font {
                        pixelSize: Theme.fontSizeExtraLarge
                        family: Theme.fontFamilyHeading
                        capitalization: Font.AllUppercase
                        bold: true
                    }
                }
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
