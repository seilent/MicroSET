pragma ComponentBehavior: Bound
import QtQuick

Rectangle {
    id: root

    property real rowY: 0
    property real rowHeight: Ui.px(Ui.rowHeight)
    property bool animated: true

    x: Ui.px(Ui.hubEdge - Ui.focusBarOffset)
    y: root.rowY
    width: Ui.px(Ui.focusBarWidth)
    height: root.rowHeight
    color: Ui.textFull

    Behavior on y {
        enabled: root.animated
        NumberAnimation {
            duration: Motion.focusCursor
            easing.type: Motion.sine
        }
    }

    Behavior on x {
        enabled: root.animated
        NumberAnimation {
            duration: Motion.focusCursor
            easing.type: Motion.sine
        }
    }
}
