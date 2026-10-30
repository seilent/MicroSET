pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool shown: false
    property bool screenOff: false
    property var available: []
    property int focusIndex: 0
    property real progress: 0
    property bool opening: true
    property bool pressed: false

    readonly property var letters: root.alphabet()
    readonly property int cols: Ui.jumpCols
    readonly property int offset: Ui.jumpExitCells
    readonly property real tile: Ui.px(Ui.jumpTile)
    readonly property real gap: Ui.px(Ui.jumpGap)
    readonly property real pitch: root.tile + root.gap
    readonly property real gridX: Ui.px(Ui.edge)
    readonly property real gridY: Ui.px(Ui.jumpTop)
    readonly property bool animating: progressAnim.running
    readonly property int openDuration: Motion.swivelIn
    readonly property int closeDuration: Motion.swivelOut
    readonly property real curved: root.opening ? Motion.expOut6(root.progress) : Motion.expIn1(1 - root.progress)
    readonly property real angle: root.opening
        ? -Motion.swivelAngle * (1 - root.curved)
        : Motion.swivelAngle * root.curved
    readonly property real visualOpacity: root.opening ? root.curved : 1 - root.curved
    readonly property string focusLetter: root.letters[root.focusIndex] === undefined ? "" : root.letters[root.focusIndex]
    readonly property int exitIndex: -1
    readonly property bool exitFocused: root.shown && root.focusIndex === root.exitIndex
    property alias exitText: exitText
    property alias exitBar: exitBar
    property alias cells: cells
    property alias swivel: swivel
    readonly property string touchKind: "block"

    signal jumpTo(string letter)
    signal closed()

    visible: root.progress > 0 && !root.screenOff

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (!root.shown)
            return ""
        switch (kind) {
        case "letter":
            root.focusIndex = index
            return "a"
        case "exit":
            root.focusIndex = root.exitIndex
            return "a"
        default:
            return ""
        }
    }

    function alphabet(): var {
        const out = ["#"]
        for (let i = 0; i < 26; i++)
            out.push(String.fromCharCode(97 + i))
        return out.concat(["あ", "か", "さ", "た", "な", "は", "ま", "や", "ら", "わ", "漢"])
    }

    function has(letter: string): bool {
        return root.available.indexOf(letter) >= 0
    }

    function cellX(index: int): real {
        return root.gridX + ((index + root.offset) % root.cols) * root.pitch
    }

    function cellY(index: int): real {
        return root.gridY + Math.floor((index + root.offset) / root.cols) * root.pitch
    }

    function cellAt(index: int): Item {
        return cells.itemAt(index)
    }

    function firstAvailable(): int {
        for (let i = 0; i < root.letters.length; i++) {
            if (root.has(root.letters[i]))
                return i
        }
        return -1
    }

    function stepLinear(delta: int): bool {
        for (let i = root.focusIndex + delta; i >= 0 && i < root.letters.length; i += delta) {
            if (root.has(root.letters[i])) {
                root.focusIndex = i
                return true
            }
        }
        if (delta < 0 && root.focusIndex !== root.exitIndex) {
            root.focusIndex = root.exitIndex
            return true
        }
        return false
    }

    function stepRow(delta: int): bool {
        const step = delta * root.cols
        const slot = root.focusIndex === root.exitIndex ? 0 : root.focusIndex + root.offset
        for (let s = slot + step; s >= 0; s += step) {
            const i = s - root.offset
            if (i >= root.letters.length)
                return false
            if (i < 0) {
                if (s !== 0)
                    return false
                root.focusIndex = root.exitIndex
                return true
            }
            if (root.has(root.letters[i])) {
                root.focusIndex = i
                return true
            }
        }
        return false
    }

    function animateTo(value: real, duration: int) {
        progressAnim.stop()
        if (root.screenOff || value === root.progress) {
            root.progress = value
            return
        }
        progressAnim.duration = duration
        progressAnim.from = root.progress
        progressAnim.to = value
        progressAnim.start()
    }

    function open(letters: var, letter: string) {
        root.available = letters === undefined || letters === null ? [] : letters
        const wanted = root.letters.indexOf(letter)
        const start = wanted >= 0 && root.has(letter) ? wanted : root.firstAvailable()
        root.focusIndex = start < 0 ? root.exitIndex : start
        root.shown = true
        root.opening = true
        root.animateTo(1, root.openDuration)
    }

    function close() {
        if (!root.shown)
            return
        root.shown = false
        root.opening = false
        root.animateTo(0, root.closeDuration)
        root.closed()
    }

    function complete() {
        if (progressAnim.running)
            progressAnim.complete()
    }

    function handleAction(name: string, repeat: bool): bool {
        if (!root.shown)
            return false
        switch (name) {
        case "up":
            return root.stepRow(-1)
        case "down":
            return root.stepRow(1)
        case "left":
            return root.stepLinear(-1)
        case "right":
            return root.stepLinear(1)
        case "a":
            if (repeat)
                return true
            if (root.focusIndex === root.exitIndex) {
                root.close()
                return true
            }
            if (root.focusLetter !== "" && root.has(root.focusLetter)) {
                root.jumpTo(root.focusLetter)
                root.close()
            }
            return true
        case "b":
        case "back":
        case "start":
            if (!repeat)
                root.close()
            return true
        default:
            return true
        }
    }

    onScreenOffChanged: {
        if (root.screenOff) {
            progressAnim.stop()
            root.progress = root.shown ? 1 : 0
            root.opening = root.shown
        }
    }

    NumberAnimation {
        id: progressAnim

        target: root
        property: "progress"
        easing.type: Motion.linear
    }

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
        opacity: root.visualOpacity
    }

    Turnstile {
        id: swivel

        anchors.fill: parent
        axis: Qt.XAxis
        hinge: root.height / 2
        angle: root.angle
        opacity: root.visualOpacity

        Text {
            id: exitText

            readonly property string touchKind: root.shown ? "exit" : ""

            x: root.gridX
            y: root.gridY + (root.tile - exitText.height) / 2
            text: "exit"
            color: Ui.white(root.exitFocused ? Ui.alphaFull : Ui.overlayLabel.alpha)
            font: Ui.font(Ui.overlayLabel)
        }

        Rectangle {
            id: exitBar

            x: root.gridX - Ui.px(Ui.focusBarOffset)
            y: root.gridY
            width: Ui.px(Ui.focusBarWidth)
            height: root.tile
            color: Ui.white(Ui.alphaFull)
            opacity: root.exitFocused ? 1 : 0
        }

        Repeater {
            id: cells

            model: root.letters

            delegate: LetterTile {
                id: cell

                required property int index
                required property var modelData
                readonly property bool available: root.has(cell.modelData)
                readonly property string touchKind: root.shown && cell.available ? "letter" : ""
                readonly property int touchIndex: cell.index

                x: root.cellX(cell.index)
                y: root.cellY(cell.index)
                size: Ui.jumpTile
                letter: cell.modelData
                empty: !cell.available
                focused: root.shown && cell.available && root.focusIndex === cell.index
                pressed: cell.focused && root.pressed
                animated: !root.screenOff
            }
        }
    }
}
