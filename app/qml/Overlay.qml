pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

Window {
    id: root

    required property QtObject overlay
    required property QtObject model
    property QtObject pad: null
    property bool screenshot: false
    property int shotWidth: Screen.width
    property int shotHeight: Screen.height
    property string shotState: "top"
    readonly property int shotSettle: Math.max(Motion.focusCursor, Motion.focusAlpha)
    readonly property bool portraitScreen: root.height > root.width
    readonly property bool following: root.portraitScreen
        && (root.screenshot || root.overlay.shown || root.overlay.hintShown)
    readonly property string rotation: root.model === null ? "" : String(root.model.value("display.rotation", root.model.revision))
    readonly property string holdText: root.overlay.hintKind === "undervolt" ? "Hold A to apply undervolt"
        : root.overlay.hintDevice === "" ? "Hold A to format" : "Hold A to format " + root.overlay.hintDevice
    readonly property real stripDepth: Ui.px(Ui.toastHeight)
    property alias view: view
    property alias router: router
    property alias toastWindow: toastWindow
    property alias hint: hint

    width: root.screenshot ? root.shotWidth : Screen.width
    height: root.screenshot ? root.shotHeight : Screen.height
    visible: root.screenshot && root.shotState !== "hint" && root.shotState !== "hint-portrait"
    color: Ui.bg

    onWidthChanged: Qt.callLater(root.syncSurface)
    onHeightChanged: Qt.callLater(root.syncSurface)
    onRotationChanged: Qt.callLater(root.syncSurface)
    onFollowingChanged: {
        if (root.model === null)
            return
        if (root.following)
            root.model.hold("display.rotation")
        else
            root.model.drop("display.rotation")
    }

    Component.onCompleted: {
        root.syncSurface()
        if (root.screenshot)
            Qt.callLater(root.applyShot)
    }

    function turnFor(name: string): int {
        switch (name) {
        case "portrait-flipped":
            return 2
        case "landscape":
            return Ui.landscapeTurn
        case "landscape-flipped":
            return (Ui.landscapeTurn + 2) % 4
        default:
            return 0
        }
    }

    function syncSurface(): void {
        const active = root.portraitScreen
        Ui.setSurface(root.width, root.height)
        Ui.turn = active ? root.turnFor(root.rotation) : 0
        Ui.orientationActive = active
    }

    function applyShot(): void {
        if (root.shotState === "audio") {
            view.open("audio")
        } else if (root.shotState === "sheet" || root.shotState === "sheet-portrait") {
            view.open("audio")
            const page = view.stack.currentItem
            page.focusIndex = page.indexOfKey("audio.volume_step")
            Qt.callLater(page.activate)
        } else if (root.shotState === "undervolt-after-sheet") {
            view.open("power")
            const page = view.stack.currentItem
            page.focusIndex = page.indexOfKey("power.gpu_undervolt")
            Qt.callLater(() => {
                page.keepFocusVisible()
                page.activate()
                page.handleAction("down", false)
                page.handleAction("a", false)
            })
        } else if (root.shotState === "game-page") {
            view.open("app.292120")
            const page = view.stack.currentItem
            Qt.callLater(() => {
                page.focusIndex = page.rows.length - 1
                page.completeScroll()
                page.flick.contentY = page.maxContentY()
            })
        } else if (root.shotState === "hint-page") {
            view.toast.show("", root.holdText)
            view.toast.complete()
        } else if (root.shotState === "hint" || root.shotState === "hint-portrait") {
            toastWindow.visible = true
            hint.show("", root.holdText)
            hint.complete()
        }
    }

    Binding {
        target: view.toast
        property: "sticky"
        value: root.overlay.hint
    }

    Timer {
        id: holdBack

        interval: Motion.toastHold
        repeat: false
        onTriggered: {
            if (root.overlay.hint && root.overlay.mapped && view.toast.title !== root.holdText)
                view.toast.show("", root.holdText)
        }
    }

    Connections {
        target: view.toast

        function onTitleChanged() {
            if (root.overlay.hint && view.toast.title !== root.holdText)
                holdBack.restart()
        }
    }

    Connections {
        target: root.pad
        ignoreUnknownSignals: true

        function onHoldingChanged() {
            if (!root.pad.holding)
                view.disarm()
        }
    }

    Binding {
        target: root.contentItem
        property: "opacity"
        value: root.overlay.opacity
        when: !root.screenshot
    }

    Connections {
        target: root.overlay

        function onPageHint() {
            view.toast.show("", root.holdText)
        }

        function onShownChanged() {
            if (!root.overlay.shown && root.model !== null)
                root.model.endPreviews()
        }

        function onHintShownChanged() {
            if (root.overlay.hintShown)
                hint.show("", root.holdText)
            else
                hint.hide()
        }
    }

    Item {
        id: rotor

        width: Ui.windowW
        height: Ui.windowH
        x: Math.round((root.width - rotor.width) / 2)
        y: Math.round((root.height - rotor.height) / 2)
        rotation: Ui.turn * 90

        Rectangle {
            anchors.fill: parent
            color: Ui.bg
        }

        SettingsView {
            id: view

            objectName: "settingsView"
            anchors.fill: parent
            model: root.model
            animated: !root.screenshot
            overlay: true
            onHideRequested: root.overlay.hide()
        }

        PadRouter {
            id: router

            anchors.fill: parent
            stack: view.stack
            pad: root.pad
            ignored: root.overlay.hint ? ["a"] : []
            focus: true
        }
    }

    Window {
        id: toastWindow

        readonly property int stripWidth: Ui.turn % 2 === 1 ? root.stripDepth : root.width
        readonly property int stripHeight: Ui.turn % 2 === 1 ? root.height : root.stripDepth

        x: Ui.turn === 1 ? root.width - root.stripDepth : 0
        y: Ui.turn === 2 ? root.height - root.stripDepth : 0
        width: toastWindow.stripWidth
        height: toastWindow.stripHeight
        minimumWidth: toastWindow.stripWidth
        maximumWidth: toastWindow.stripWidth
        minimumHeight: toastWindow.stripHeight
        maximumHeight: toastWindow.stripHeight
        flags: Qt.FramelessWindowHint
        transientParent: null
        color: Ui.black
        visible: false

        Item {
            id: stripRotor

            width: Ui.windowW
            height: root.stripDepth
            x: Math.round((toastWindow.width - stripRotor.width) / 2)
            y: Math.round((toastWindow.height - stripRotor.height) / 2)
            rotation: Ui.turn * 90

            Toast {
                id: hint

                x: 0
                y: 0
                width: stripRotor.width
                screenOff: false
                sticky: root.overlay.hint
                onPhaseChanged: {
                    if (hint.phase === "hidden" && hint.progress <= 0 && root.overlay.hintShown)
                        root.overlay.hintDone()
                }
            }
        }
    }
}
