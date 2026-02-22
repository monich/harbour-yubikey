import QtQuick 2.0
import Sailfish.Silica 1.0

DockedPanel {
    id: thisItem

    property alias text: label.text
    property alias iconSource: icon.source
    property alias minDisplayInterval: minDisplayIntervalTimer.interval
    property bool isPortrait: true
    property bool autoHide: true

    width: isPortrait ? parent.width : Math.ceil(2 * parent.width / 3)
    anchors.horizontalCenter: parent.horizontalCenter
    height: content.height + _padding
    animationDuration: 250
    dock: Dock.Top
    background: null
    modal: true

    readonly property bool _fullyOpen: open && !moving
    readonly property int _padding: Theme.paddingMedium

    onAutoHideChanged: {
        if (autoHide) {
            if (_fullyOpen && !minDisplayIntervalTimer.running) {
                hide(false)
            }
        } else {
            minDisplayIntervalTimer.stop()
        }
    }

    on_FullyOpenChanged: {
        if (_fullyOpen) {
            minDisplayIntervalTimer.restart()
        }
    }

    onOpenChanged: {
        if (!open) {
            minDisplayIntervalTimer.stop()
        }
    }

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
            source: "images/yubikey-ok.svg"
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
            wrapMode: Text.WordWrap
            color: Theme.highlightColor
        }
    }

    Timer {
        id: minDisplayIntervalTimer

        interval: 2000
        onTriggered: {
            if (autoHide) {
                hide(false)
            }
        }
    }
}
