pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

Window {
    id: root

    required property QtObject recovery
    property QtObject pad: null
    property bool screenshot: false
    property int shotWidth: Screen.width
    property int shotHeight: Screen.height
    property string shotState: "browse"
    property int shotFocus: 0
    readonly property int shotSettle: Math.max(Motion.focusCursor, Motion.focusAlpha)

    width: root.screenshot ? root.shotWidth : Screen.width
    height: root.screenshot ? root.shotHeight : Screen.height
    visible: true
    visibility: root.screenshot ? Window.Windowed : Window.FullScreen
    color: Ui.bg

    onWidthChanged: Ui.setSurface(root.width, root.height)
    onHeightChanged: Ui.setSurface(root.width, root.height)

    Component.onCompleted: {
        Ui.setSurface(root.width, root.height)
        if (!root.screenshot)
            return
        screen.list.snapTo(root.shotFocus)
        if (root.shotState === "dialog") {
            screen.confirmRollback()
            screen.dialog.complete()
        } else if (root.shotState === "hold") {
            screen.confirmRollback()
            screen.dialog.focusIndex = screen.dialog.rebootIndex
            screen.dialog.pressed = true
            screen.dialog.complete()
        } else if (root.shotState === "toast") {
            screen.toast.show("power off", "Interactive authentication required.")
            screen.toast.complete()
        } else if (root.shotState === "stock-gpu") {
            screen.list.snapTo(screen.actions.findIndex(a => a.key === "stock"))
        } else if (root.shotState === "stock-gpu-done") {
            screen.list.snapTo(0)
            screen.toast.show("stock GPU clocks", "Stock from the next boot")
            screen.toast.complete()
        }
    }

    RecoveryScreen {
        id: screen

        objectName: "recoveryScreen"
        anchors.fill: parent
        recovery: root.recovery
        animated: !root.screenshot
    }

    Item {
        id: stack

        property Item currentItem: screen
        property bool inputLocked: false
    }

    PadRouter {
        id: router

        anchors.fill: parent
        stack: stack
        pad: root.pad
        modal: screen.dialog
        focus: true
    }
}
