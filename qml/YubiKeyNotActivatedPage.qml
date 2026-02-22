import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

Page {
    id: thisPage

    property var yubiKey

    // Using full height for layout purposes, the actual page height may be
    // initially smaller if we are transitioning from the page which had VKB
    // open (which squeezes the page vertically)
    readonly property int _portraitHeight: Screen.height
    readonly property int _landscapeHeight: Screen.width

    function isOk() {
        return yubiKey.authAccess === YubiKey.AccessNotActivated;
    }

    onStatusChanged: {
        if (status === PageStatus.Active) {
            backNavigation = Qt.binding(function() { return !NfcAdapter.targetPresent })
        }
    }

    Item {
        id: iconContainer

        anchors.top: parent.top

        Image {
            id: icon

            anchors.centerIn: parent
            source: "images/yubikey-question.svg"
            sourceSize.height: Math.min(parent.height - 2 * Theme.paddingLarge, Theme.iconSizeExtraLarge)
        }
    }

    Item {
        id: contentContainer

        anchors {
            right: parent.right
            bottom: parent.top // + orientation specific offset
        }

        InfoLabel {
            id: mainLabel

            // y coordinate of the vertical center in page coordinates
            readonly property int _yAbsCenter: parent.y + y + height/2

            anchors.top: parent.top
            horizontalAlignment: isPortrait ? Text.AlignHCenter : Text.AlignLeft

            //: Info label
            //% "Connect your YubiKey to any USB power source, such as a computer, for at least 3 seconds."
            text: qsTrId("yubikey-not_activated-info_label_1")
        }

        InfoLabel {
            anchors {
                bottom: parent.bottom
                bottomMargin: Theme.paddingLarge
            }
            font.pixelSize: Theme.fontSizeMedium
            verticalAlignment: Text.AlignBottom
            horizontalAlignment: mainLabel.horizontalAlignment

            //: Info label
            //% "Once powered, NFC will be activated and ready for use."
            text: qsTrId("yubikey-not_activated-info_label_2")
        }
    }

    states: [
        State {
            name: "portrait"
            when: isPortrait
            changes: [
                PropertyChanges {
                    target: iconContainer
                    width: thisPage.width
                    height: Math.max(2 * Theme.iconSizeExtraLarge, Math.round(_portraitHeight/3))
                },
                PropertyChanges {
                    target: icon
                    anchors {
                        verticalCenterOffset: 0
                        horizontalCenterOffset: 0
                    }
                },
                PropertyChanges {
                    target: contentContainer
                    anchors {
                        topMargin: 0
                        bottomMargin: -_portraitHeight // relative to top
                    }
                },
                AnchorChanges {
                    target: contentContainer
                    anchors {
                        top: iconContainer.bottom
                        left: parent.left
                    }
                }
            ]
        },
        State {
            name: "landscape"
            when: !isPortrait
            changes: [
                PropertyChanges {
                    target: iconContainer
                    width: 2 * Theme.iconSizeExtraLarge
                    height: _landscapeHeight
                },
                PropertyChanges {
                    target: icon
                    anchors {
                        verticalCenterOffset: mainLabel._yAbsCenter - _landscapeHeight/2
                        horizontalCenterOffset: Theme.horizontalPageMargin/2
                    }
                },
                PropertyChanges {
                    target: contentContainer
                    anchors {
                        topMargin: Math.round(_landscapeHeight/3 - mainLabel.height/2)
                        bottomMargin: -_landscapeHeight // relative to top
                    }
                },
                AnchorChanges {
                    target: contentContainer
                    anchors {
                        top: parent.top
                        left: iconContainer.right
                    }
                }
            ]
        }
    ]
}
