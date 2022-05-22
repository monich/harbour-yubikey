import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

ApplicationWindow {
    id: thisApp

    allowedOrientations: Orientation.All

    //: Application title
    //% "YubiKey"
    readonly property string title: qsTrId("yubikey-app_name")

    property Page yubiKeyPage: !!mainPage ? mainPage.yubiKeyPage : null
    property Page mainPage

    initialPage: Component {
        MainPage {
            id: mainPage

            allowedOrientations: thisApp.allowedOrientations
            Component.onCompleted: thisApp.mainPage = mainPage
        }
    }

    cover: Component {
        CoverPage {
            haveYubiKey: !!yubiKeyPage
            name: yubiKeyPage ? yubiKeyPage.favoriteName : ""
            password: yubiKeyPage ? yubiKeyPage.favoritePassword : ""
            passwordType: yubiKeyPage ? yubiKeyPage.favoriteTokenType : YubiKeyCard.TypeUnknown
            passwordTimeLeft: yubiKeyPage ? yubiKeyPage.totpTimeLeft : 0
            passwordMarkedForRefresh: yubiKeyPage && yubiKeyPage.favoriteMarkedForRefresh
            onClearCardInfo: pageStack.pop(mainPage, PageStackAction.Immediate)
        }
    }

    NfcMode {
        enableModes: NfcSystem.ReaderWriter
        disableModes: NfcSystem.P2PInitiator + NfcSystem.P2PTarget + NfcSystem.CardEmulation
        active: Qt.application.active
    }
}
