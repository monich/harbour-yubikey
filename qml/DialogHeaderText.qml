import QtQuick 2.0
import Sailfish.Silica 1.0

import "harbour"

HarbourMarqueeText {
    property Item headerItem
    property Item textItem

    x: headerItem.extraContent.x
    width: headerItem.extraContent.width
    horizontalAlignment: Text.AlignHCenter
    anchors.verticalCenter: headerItem.verticalCenter
    color: Theme.secondaryHighlightColor
    font {
        pixelSize: headerItem.dialog.isLandscape ? Theme.fontSizeLarge : Theme.fontSizeExtraLarge
        family: Theme.fontFamilyHeading
    }
    text: textItem.text
    visible: opacity > 0
    opacity: 1 - textItem.opacity
    autoStart: headerItem.dialog.status === DialogStatus.Opened
}
