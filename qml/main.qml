import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

ApplicationWindow {
    id: thisApp

    allowedOrientations: Orientation.All

    //: Application title
    //% "YubiKey"
    readonly property string title: qsTrId("yubikey-app_name")
    property alias yubiKey: key
    property Page mainPage: null

    initialPage: Component {
        MainPage {
            id: mainPage

            yubiKey: thisApp.yubiKey
            allowedOrientations: thisApp.allowedOrientations
            Component.onCompleted: thisApp.mainPage = mainPage
        }
    }

    cover: Component {
        CoverPage {
            yubiKey: thisApp.yubiKey
            mainPage: thisApp.mainPage
            onClearCardInfo: pageStack.pop(thisApp.mainPage, PageStackAction.Immediate)
        }
    }

    NfcMode {
        enableModes: NfcSystem.ReaderWriter
        disableModes: NfcSystem.P2PInitiator + NfcSystem.P2PTarget + NfcSystem.CardEmulation
        active: Qt.application.active
    }

    YubiKeyIoManager {
        id: ioManager
    }

    YubiKeyNdefHandler {
        yubiKeyIo: ioManager.yubiKeyIo
    }

    YubiKey {
        id: key

        yubiKeyIo: ioManager.yubiKeyIo
    }
}
