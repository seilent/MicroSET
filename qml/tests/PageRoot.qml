pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

Window {
    id: root

    required property QtObject model
    property bool screenshot: false
    property int shotWidth: 960
    property int shotHeight: 640
    property string shotState: "top"
    readonly property int shotSettle: Math.max(Motion.focusCursor, Motion.focusAlpha)
    readonly property var groupStates: ({
        audio: "audio", display: "display", rgb: "stick leds", "rgb-off": "stick leds", power: "power", system: "system",
        updates: "updates",
        "long-label": "group with a long title", merged: "display"
    })
    property alias view: view

    width: root.shotWidth
    height: root.shotHeight
    visible: true
    color: Ui.bg

    onWidthChanged: Ui.setSurface(root.width, root.height)
    onHeightChanged: Ui.setSurface(root.width, root.height)

    function edit(group: string, key: string, steps: int): void {
        view.open(group)
        const page = view.stack.currentItem
        page.focusIndex = page.indexOfKey(key)
        page.activate()
        for (let i = 0; i < Math.abs(steps); i++)
            page.step(Math.sign(steps))
        Qt.callLater(page.keepFocusVisible)
    }

    function bottom(): void {
        const page = view.stack.currentItem
        page.completeScroll()
        page.flick.contentY = page.maxContentY()
    }

    function apply(): void {
        const group = root.groupStates[root.shotState]
        if (group !== undefined) {
            view.open(group)
        } else if (root.shotState.startsWith("undervolt")) {
            view.open("power")
            const page = view.stack.currentItem
            page.focusIndex = page.indexOfKey(root.shotState === "undervolt-reason-focus"
                ? "power.gpu_tuning_fallback" : "power.gpu_undervolt")
            Qt.callLater(root.bottom)
            const toasts = {
                "undervolt-hold": ["", "Hold A to apply undervolt"],
                "undervolt-refused": ["GPU undervolt", "A not held"],
                "undervolt-busy": ["GPU undervolt", "another hold is waiting"]
            }
            const toast = toasts[root.shotState]
            if (toast !== undefined) {
                view.toast.show(toast[0], toast[1])
                view.toast.complete()
            }
        } else if (root.shotState === "edit") {
            root.edit("audio", "audio.output", 1)
        } else if (root.shotState === "hue-edit") {
            root.edit("stick leds", "rgb.hue", 1)
        } else if (root.shotState === "long-value") {
            view.open("audio")
            const page = view.stack.currentItem
            page.focusIndex = page.indexOfKey("audio.eq.headphones.name")
            Qt.callLater(page.keepFocusVisible)
        } else if (root.shotState === "draft-edit") {
            root.edit("system", "session.default", -1)
        } else if (root.shotState === "toast") {
            view.open("audio")
            view.toast.show("Output", "set failed")
            view.toast.complete()
        }
    }

    Component.onCompleted: {
        Ui.setSurface(root.width, root.height)
        if (root.screenshot)
            Qt.callLater(root.apply)
    }

    SettingsView {
        id: view

        objectName: "settingsView"
        anchors.fill: parent
        model: root.model
        animated: !root.screenshot
    }

    PadRouter {
        id: router

        anchors.fill: parent
        stack: view.stack
        focus: true
    }
}
