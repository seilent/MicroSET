import QtQuick
import MicroSet

PadRouter {
    id: router

    objectName: "router"

    required property QtObject launcher
    property Item home: null
    property Item allApps: null
    property Item sheet: null
    property Item dialog: null
    property Item powerPage: null

    readonly property var turnOrder: ["up", "right", "down", "left"]
    readonly property var faces: ["y", "b", "a", "x"]
    readonly property int turn: router.launcher === null ? 0 : router.launcher.padTurn
    readonly property bool sheetUp: router.sheet !== null && router.sheet.shown === true
    readonly property bool dialogUp: router.dialog !== null && router.dialog.shown === true
    readonly property bool powerUp: router.powerPage !== null && router.powerPage.shown === true
    readonly property var chain: [router.dialog, router.sheet, router.powerPage]

    keyboard: false
    modal: router.dialogUp ? router.dialog : router.sheetUp ? router.sheet : router.powerUp ? router.powerPage : null

    HomeStack {
        id: homeStack

        home: router.home
        allApps: router.allApps
        page: router.launcher === null ? "home" : router.launcher.page
    }

    stack: homeStack

    function padName(raw: string): string {
        const dir = router.turnOrder.indexOf(raw)
        if (dir >= 0)
            return router.turnOrder[(dir + router.turn) % 4]
        const face = router.faces.indexOf(raw)
        if (face >= 0)
            return router.faces[(face + router.turn) % 4]
        return raw
    }

    function dispatch(name: string, repeat: bool): bool {
        if (name === "")
            return false
        if (router.screenOff || router.locked || router.saverUp) {
            router.dropped(name, repeat)
            return false
        }
        if (router.launcher === null || router.launcher.launching || !router.launcher.content) {
            router.dropped(name, repeat)
            return false
        }
        router.launcher.pulse()
        for (const modal of router.chain) {
            if (modal === null || modal === undefined || modal.shown !== true)
                continue
            const taken = typeof modal.handleAction === "function" ? modal.handleAction(name, repeat) : false
            router.routed(name, repeat, taken)
            return taken
        }
        const target = router.page()
        let handled = false
        if (target !== null && target === router.home)
            handled = router.launcher.handleAction(name, repeat)
        else if (target !== null && typeof target.handleAction === "function")
            handled = target.handleAction(name, repeat)
        if (!handled)
            router.unhandled(name, repeat)
        router.routed(name, repeat, handled)
        return handled
    }

    Connections {
        target: router.pad
        ignoreUnknownSignals: true

        function onAction(action, repeat) {
            if (repeat)
                return
            const raw = router.pad.actionName(action)
            if (router.turnOrder.indexOf(raw) < 0 && router.faces.indexOf(raw) < 0)
                return
            if (router.ignored.indexOf(raw) >= 0 || router.chordHeld[raw] === true)
                return
            console.info("launcher: pad", raw, "routed as", router.heldName(raw), "turn", router.turn)
        }
    }
}
