import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.yubikey 1.0

YubiKeyWaitPage {
    id: thisPage

    property var yubiKey
    property var opIds: []
    property alias trackerState: tracker.state

    signal opFailed(var index)
    signal opsFinished(var code)

    yubiKeyPresent: yubiKey && yubiKey.present

    property int _currentOp: -1

    Component.onCompleted: _nextOp()

    onOpIdsChanged: {
        progressValue = busyProgress
        _currentOp = -1
        _nextOp()
    }

    onStatusChanged: {
        if (status === PageStatus.Deactivating) {
            _cancelRemainingOps()
        }
    }

    Connections {
        target: yubiKey
        onInvalidYubiKeyConnected: thisPage.invalidYubiKeyConnected()
    }

    function _nextOp() {
        for (_currentOp++; _currentOp < opIds.length; _currentOp++) {
            var op = yubiKey.getOp(opIds[_currentOp])
            if (op) {
                tracker.op = op
                progressValue = busyProgress + _currentOp * (1 - busyProgress) / opIds.length
                return true
            }
        }
        progressValue = 1
        return false
    }

    function _cancelRemainingOps() {
        for (_currentOp++; _currentOp < opIds.length; _currentOp++) {
            yubiKey.cancelOp(opIds[_currentOp])
        }
    }

    YubiKeyOpTracker {
        id: tracker

        onStateChanged: {
            switch (state) {
            case YubiKeyOpTracker.Failed:
                thisPage.opFailed(_currentOp)
                break
            case YubiKeyOpTracker.Finished:
                if (opResultCode !== YubiKey.Success || !_nextOp()) {
                    showNavigationIndicator = false
                    backNavigation = true
                    forwardNavigation = true
                    thisPage.opsFinished(opResultCode)
                }
                break
            }
        }
    }
}
