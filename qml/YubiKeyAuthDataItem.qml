import QtQuick 2.0
import Sailfish.Silica 1.0

ListItem {
    property alias yubiKeyId: yubiKeyIdLabel.text
    property var lastAccess

    function _dateString(date) {
        return date.toLocaleDateString(Qt.locale(), "dd.MM.yyyy")
    }

    function _timeString(time) {
        return time.toLocaleTimeString(Qt.locale(), "hh:mm")
    }

    function _dateTimeString(dateTime) {
        return isNaN(dateTime) ? "" : (_dateString(dateTime) + " " + _timeString(dateTime))
    }

    Label {
        id: yubiKeyIdLabel

        anchors {
            left: parent.left
            leftMargin: Theme.paddingMedium
            right: lastAccessLabel.left
            rightMargin: Theme.paddingMedium
        }
        x: Theme.paddingMedium
        height: parent.height
        verticalAlignment: Text.AlignVCenter
        truncationMode: TruncationMode.Fade
    }

    Label {
        id: lastAccessLabel

        anchors {
            right: parent.right
            rightMargin: yubiKeyIdLabel.anchors.leftMargin
        }
        height: parent.height
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
        color: Theme.secondaryHighlightColor
        text: _dateTimeString(lastAccess)
    }
}
