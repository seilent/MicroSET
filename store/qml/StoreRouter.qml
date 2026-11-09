pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

PadRouter {
    id: router

    objectName: "router"

    required property QtObject store

    readonly property var turnOrder: ["up", "right", "down", "left"]
    readonly property var faces: ["y", "b", "a", "x"]
    readonly property int turn: router.store === null ? 0 : router.store.padTurn

    keyboard: false
    screenOff: router.store === null || !router.store.shown

    function padName(raw: string): string {
        const dir = router.turnOrder.indexOf(raw)
        if (dir >= 0)
            return router.turnOrder[(dir + router.turn) % 4]
        const face = router.faces.indexOf(raw)
        if (face >= 0)
            return router.faces[(face + router.turn) % 4]
        return raw
    }
}
