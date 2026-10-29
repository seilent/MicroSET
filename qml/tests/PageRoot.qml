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
    readonly property bool motion: root.shotState === "sheet-motion"
    readonly property int shotSettle: Math.max(Motion.focusCursor, Motion.focusAlpha)
    readonly property var groupStates: ({
        audio: "audio", display: "display", rgb: "stick leds", "rgb-off": "stick leds", power: "power", system: "system",
        updates: "updates", games: "games", "games-empty": "games",
        "long-label": "group with a long title", merged: "display"
    })
    readonly property var sheetStates: ({
        "sheet-volume-step": ["audio", "audio.volume_step", 0],
        "sheet-gpu-clock": ["power", "power.gpu_clock", 0],
        "sheet-power-on": ["audio", "audio.power_on_volume", 0],
        "sheet-brightness-off-list": ["display", "display.brightness", 0],
        "sheet-saturation-moved": ["stick leds", "rgb.saturation", 2],
        "sheet-game-floor": ["power", "power.game_floor", 0],
        "sheet-undervolt": ["power", "power.gpu_undervolt", 1],
        "sheet-refused": ["audio", "audio.volume_step", 0],
        "sheet-emu-scheme": ["emulation", "microemu.esde.theme_color_scheme", 0],
        "sheet-emu-scheme-end": ["emulation", "microemu.esde.theme_color_scheme", -1],
        "sheet-motion": ["audio", "audio.volume_step", 0],
        "sheet-emu-variant": ["emulation", "microemu.esde.theme_variant", 0],
        "sheet-long-label": ["group with a long title", "fixture.long", 0],
        "sheet-tso": ["app.292120", "app.292120.fex.tso", 2],
        "sheet-tso-on": ["app.292120", "app.292120.fex.tso", 1],
        "sheet-preset": ["app.2238900", "app.2238900.fex.preset", 0],
        "sheet-profile": ["app.292120", "app.292120.profile", 0],
        "sheet-smc": ["app.292120", "app.292120.fex.smc", 1],
        "sheet-global-preset": ["games", "fex.preset", 1],
        "sheet-lsfg-mode": ["Frame generation", "lsfg.mode", 1],
        "sheet-lsfg-multiplier": ["Frame generation", "lsfg.multiplier", 0],
        "sheet-lsfg-cap": ["Frame generation", "lsfg.fps_cap", 0],
        "sheet-lsfg-flow": ["Frame generation", "lsfg.flow_scale", 0],
        "sheet-game-lsfg": ["app.2161700", "app.2161700.lsfg.state", 1],
        "sheet-game-lsfg-multiplier": ["app.2161700", "app.2161700.lsfg.multiplier", 0],
        "sheet-game-lsfg-cap": ["app.2161700", "app.2161700.lsfg.fps_cap", 0]
    })
    readonly property var gameStates: ({
        "game-profile": "app.292120", "game-overridden": "app.292120", "game-profile-off": "app.292120",
        "game-preset": "app.2238900", "game-global": "app.2238900", "game-bottom": "app.292120", "game-long": "app.251150",
        "game-lsfg": "app.2161700", "game-lsfg-blacklist": "app.2161700", "game-lsfg-disabled": "app.2161700",
        "game-lsfg-cap-off": "app.2161700"
    })
    readonly property var lsfgStates: ({
        lsfg: "lsfg-status", "lsfg-dialog": "lsfg-install", "lsfg-open": "lsfg-game-2238900",
        "lsfg-blacklist": "lsfg-game-2050650", "lsfg-off": "lsfg-games", "lsfg-bottom": "lsfg-limit",
        "lsfg-status-long": "lsfg-status", "lsfg-installing": "lsfg-status", "lsfg-sent": "lsfg-status",
        "lsfg-no-client": "lsfg-status", "lsfg-game-running": "lsfg-status", "lsfg-microsd": "lsfg-status",
        "lsfg-unknown": "lsfg-status", "lsfg-old-layer": "lsfg-status", "lsfg-install-focus": "lsfg-install",
        "lsfg-open-empty": "lsfg-proton", "lsfg-gone-long": "lsfg-game-251150", "lsfg-open-top": "lsfg-games",
        "lsfg-games-motion": "lsfg-games", "lsfg-dialog-motion": "lsfg-install"
    })
    readonly property var lsfgOpen: ["lsfg-open", "lsfg-blacklist", "lsfg-bottom", "lsfg-open-empty", "lsfg-gone-long",
        "lsfg-open-top"]
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

    function sheet(group: string, key: string, moves: int): void {
        view.open(group)
        const page = view.stack.currentItem
        page.focusIndex = page.indexOfKey(key)
        Qt.callLater(() => {
            page.keepFocusVisible()
            if (root.motion) {
                page.animated = true
                fall.start()
            }
            page.activate()
            for (let i = 0; i < Math.abs(moves); i++)
                page.handleAction(moves > 0 ? "down" : "up", false)
            if (root.shotState === "sheet-refused") {
                view.toast.show("Volume step", "set failed")
                view.toast.complete()
            }
        })
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
        } else if (root.shotState === "edit-balance") {
            root.edit("audio", "audio.balance.headphones", 2)
        } else if (root.shotState === "hue-edit") {
            root.edit("stick leds", "rgb.hue", 1)
        } else if (root.shotState === "long-value") {
            view.open("audio")
            const page = view.stack.currentItem
            page.focusIndex = page.indexOfKey("audio.eq.headphones.name")
            Qt.callLater(page.keepFocusVisible)
        } else if (root.shotState === "toast") {
            view.open("audio")
            view.toast.show("Output", "set failed")
            view.toast.complete()
        } else if (root.sheetStates[root.shotState] !== undefined) {
            const shot = root.sheetStates[root.shotState]
            root.sheet(shot[0], shot[1], shot[2])
        } else if (root.gameStates[root.shotState] !== undefined) {
            view.open(root.gameStates[root.shotState])
            if (root.shotState === "game-bottom" || root.shotState.startsWith("game-lsfg")) {
                const page = view.stack.currentItem
                page.focusIndex = page.rows.length - 2
                Qt.callLater(root.bottom)
            }
        } else if (root.lsfgStates[root.shotState] !== undefined) {
            view.open("Frame generation")
            const page = view.stack.currentItem
            if (root.lsfgOpen.indexOf(root.shotState) >= 0)
                root.model.toggle("lsfg-games")
            page.focusIndex = page.indexOfKey(root.lsfgStates[root.shotState])
            Qt.callLater(() => {
                if (root.shotState === "lsfg-bottom") {
                    root.bottom()
                } else if (root.shotState === "lsfg-open-top" || root.shotState === "lsfg-games-motion") {
                    page.completeScroll()
                    page.flick.contentY = Math.min(page.maxContentY(), page.rowTopOf(page.focusIndex))
                } else {
                    page.keepFocusVisible()
                }
                if (root.shotState === "lsfg-dialog") {
                    page.activate()
                    page.confirm.complete()
                }
                if (root.shotState.endsWith("-motion")) {
                    page.animated = true
                    later.start()
                }
            })
        } else if (root.shotState === "games-many") {
            view.open("games")
            const page = view.stack.currentItem
            page.focusIndex = page.rows.length - 1
            Qt.callLater(root.bottom)
        } else if (root.shotState === "games-bottom") {
            view.open("games")
            const page = view.stack.currentItem
            page.focusIndex = page.indexOfKey("game-2238900")
            Qt.callLater(root.bottom)
        } else if (root.shotState === "games-long") {
            view.open("games")
            const page = view.stack.currentItem
            page.focusIndex = page.indexOfKey("game-251150")
            Qt.callLater(page.keepFocusVisible)
        } else if (root.shotState === "flip-before" || root.shotState === "flip-after") {
            view.open("display")
            const page = view.stack.currentItem
            page.focusIndex = page.indexOfKey("display.auto_rotate")
            if (root.shotState === "flip-after")
                page.activate()
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

    Timer {
        id: later

        interval: Motion.listChange
        onTriggered: view.stack.currentItem.activate()
    }

    Timer {
        id: fall

        interval: Motion.sheet * 2
        onTriggered: view.stack.currentItem.handleAction("b", false)
    }

    PadRouter {
        id: router

        anchors.fill: parent
        stack: view.stack
        focus: true
    }
}
