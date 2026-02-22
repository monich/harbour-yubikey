import QtQuick 2.0
import Sailfish.Silica 1.0

// Helps to avoid
// [D] doPush:134 - Warning: cannot push while transition is in progress
// [D] doPop:492 - Warning: cannot pop while transition is in progress
// and similar issues
QtObject {
    signal execute()

    property bool _executeWhenTransitionEnds
    readonly property bool _transitionInProgress: pageStack.currentPage &&
        (pageStack.currentPage.status === PageStatus.Activating ||
         pageStack.currentPage.status === PageStatus.Deactivating)

    property var _timer: Timer {
        interval: 0
        onTriggered: {
            if (_transitionInProgress) {
                // Transition started while the timer was running
                _executeWhenTransitionEnds = true
            } else {
                execute()
            }
        }
    }

    on_TransitionInProgressChanged: {
        if (!_transitionInProgress && _executeWhenTransitionEnds) {
            _executeWhenTransitionEnds = false
            _timer.start()
        }
    }

    function schedule() {
        if (_transitionInProgress) {
            _executeWhenTransitionEnds = true
        } else {
            _timer.start()
        }
    }
}
