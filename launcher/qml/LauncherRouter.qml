import QtQuick
import MicroSet

PadRouter {
    id: router

    objectName: "router"

    required property QtObject launcher
    property Item home: null

    readonly property var turnOrder: ["up", "right", "down", "left"]
    readonly property var faces: ["y", "b", "a", "x"]
    readonly property int turn: router.launcher === null ? 0 : router.launcher.padTurn

    keyboard: false

    HomeStack {
        id: homeStack

        currentItem: router.home
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
        if (router.modalUp) {
            const taken = router.modal.handleAction(name, repeat)
            router.routed(name, repeat, taken)
            return taken
        }
        if (router.launcher === null) {
            router.dropped(name, repeat)
            return false
        }
        const handled = router.launcher.handleAction(name, repeat)
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
