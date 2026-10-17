pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: router

    property Item stack: null
    property QtObject pad: null
    property QtObject guide: null
    property Item modal: null
    property Item saver: null
    property bool screenOff: false
    property bool keyboard: true
    property bool sweeping: false
    readonly property bool locked: (router.stack !== null && router.stack.inputLocked) || router.sweeping
    readonly property bool modalUp: router.modal !== null && router.modal.shown === true
    readonly property bool saverUp: router.saver !== null && router.saver.shown === true
    readonly property bool rotating: Ui.orientationActive && !router.screenOff
    readonly property var keyMap: ({
        [Qt.Key_Up]: "up",
        [Qt.Key_Down]: "down",
        [Qt.Key_Left]: "left",
        [Qt.Key_Right]: "right",
        [Qt.Key_Return]: "a",
        [Qt.Key_Enter]: "a",
        [Qt.Key_Escape]: "b",
        [Qt.Key_Backspace]: "b",
        [Qt.Key_X]: "x",
        [Qt.Key_Y]: "y",
        [Qt.Key_Q]: "lb",
        [Qt.Key_E]: "rb",
        [Qt.Key_PageUp]: "lt",
        [Qt.Key_PageDown]: "rt",
        [Qt.Key_Space]: "start",
        [Qt.Key_Tab]: "back"
    })
    readonly property var directions: ["up", "down", "left", "right"]
    property var held: ({})
    property var heldCount: ({})
    property var chordHeld: ({})

    signal routed(string name, bool repeat, bool handled)
    signal dropped(string name, bool repeat)
    signal nowPlaying()
    signal view(string name)
    signal unhandled(string name, bool repeat)
    signal rotate(string raw)

    focus: true
    activeFocusOnTab: false

    function keyName(key: int): string {
        const name = router.keyMap[key]
        return name === undefined ? "" : name
    }

    function chord(raw: string): bool {
        if (router.directions.indexOf(raw) < 0)
            return false
        if (Object.keys(router.chordHeld).length > 0) {
            router.chordHeld[raw] = true
            return true
        }
        if (!router.rotating || router.guide === null || typeof router.guide.take !== "function" || !router.guide.take())
            return false
        router.chordHeld[raw] = true
        router.rotate(raw)
        return true
    }

    function heldName(raw: string): string {
        const name = router.held[raw]
        return name === undefined ? Ui.padButton(raw) : name
    }

    function page(): Item {
        if (router.stack === null)
            return null
        return router.stack.currentItem
    }

    function dispatch(name: string, repeat: bool): bool {
        if (name === "")
            return false
        if (router.screenOff || router.locked) {
            router.dropped(name, repeat)
            return false
        }
        if (router.saverUp) {
            router.saver.hide()
            router.routed(name, repeat, true)
            return true
        }
        if (router.modalUp) {
            const taken = router.modal.handleAction(name, repeat)
            router.routed(name, repeat, taken)
            return taken
        }
        const target = router.page()
        let handled = false
        if (target !== null && typeof target.handleAction === "function")
            handled = target.handleAction(name, repeat)
        if (!handled && !repeat) {
            if (name === "start") {
                router.nowPlaying()
                handled = true
            } else if (name === "back") {
                router.view(name)
                handled = true
            }
        }
        if (!handled)
            router.unhandled(name, repeat)
        router.routed(name, repeat, handled)
        return handled
    }

    function press(name: string) {
        if (name === "" || router.screenOff || router.locked || router.modalUp || router.saverUp)
            return
        const target = router.page()
        if (target !== null && typeof target.press === "function")
            target.press(name)
    }

    function release(name: string) {
        if (name === "")
            return
        const target = router.page()
        if (target !== null && typeof target.release === "function")
            target.release(name)
    }

    Connections {
        target: router.pad
        ignoreUnknownSignals: true

        function onAction(action, repeat) {
            const raw = router.pad.actionName(action)
            if (router.chordHeld[raw] === true)
                return
            router.dispatch(router.heldName(raw), repeat)
        }

        function onPressed(action) {
            const raw = router.pad.actionName(action)
            if (router.chord(raw))
                return
            if (router.held[raw] !== undefined)
                return
            const name = Ui.padButton(raw)
            const count = router.heldCount[name] === undefined ? 0 : router.heldCount[name]
            router.held[raw] = name
            router.heldCount[name] = count + 1
            if (count === 0)
                router.press(name)
        }

        function onReleased(action) {
            const raw = router.pad.actionName(action)
            if (router.chordHeld[raw] === true) {
                delete router.chordHeld[raw]
                return
            }
            const name = router.heldName(raw)
            if (router.held[raw] === undefined) {
                router.release(name)
                return
            }
            delete router.held[raw]
            const count = router.heldCount[name] - 1
            if (count > 0) {
                router.heldCount[name] = count
                return
            }
            delete router.heldCount[name]
            router.release(name)
        }
    }

    Keys.onPressed: (event) => {
        if (!router.keyboard)
            return
        const name = router.keyName(event.key)
        if (name === "")
            return
        event.accepted = true
        if (event.isAutoRepeat && router.directions.indexOf(name) < 0)
            return
        if (!event.isAutoRepeat)
            router.press(name)
        router.dispatch(name, event.isAutoRepeat)
    }

    Keys.onReleased: (event) => {
        if (!router.keyboard)
            return
        const name = router.keyName(event.key)
        if (name === "")
            return
        event.accepted = true
        if (event.isAutoRepeat)
            return
        router.release(name)
    }
}
