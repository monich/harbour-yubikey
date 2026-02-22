import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

YubiKeyWaitPage {
    id: thisPage

    property alias op: tracker.op

    progressValue: (tracker.state === YubiKeyOpTracker.Active ||
                    tracker.state === YubiKeyOpTracker.Finished) ? 1. : busyProgress

    signal opFailed()
    signal opFinished()

    onStatusChanged: {
        if (status === PageStatus.Deactivating) {
            tracker.cancelOp()
        }
    }

    YubiKeyOpTracker {
        id: tracker

        onStateChanged: {
            switch (state) {
            case YubiKeyOpTracker.Failed:
                thisPage.opFailed()
                break
            case YubiKeyOpTracker.Finished:
                showNavigationIndicator = false
                backNavigation = true
                forwardNavigation = true
                thisPage.opFinished()
                break
            }
        }
    }
}
