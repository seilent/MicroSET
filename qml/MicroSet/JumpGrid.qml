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
    readonly property real pitch: Ui.px(Ui.jumpPitch)
    readonly property real gridX: Ui.px(Ui.jumpLeft)
    readonly property real gridY: Ui.px(Ui.jumpTop)
    readonly property bool animating: progressAnim.running
    readonly property int openDuration: Motion.jumpIn
    readonly property int closeDuration: Motion.jumpOut
    readonly property real visualOpacity: root.progress
    readonly property string focusLetter: root.letters[root.focusIndex] === undefined ? "" : root.letters[root.focusIndex]
    readonly property int exitIndex: -1
    readonly property bool exitFocused: root.focusIndex === root.exitIndex
    property alias exitText: exitText
    property alias exitCell: exitCell
    property alias cells: cells
    property alias face: face
    property alias fade: progressAnim
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
        return cells.itemAt(index)?.tile ?? null
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
        if (progressAnim.running && !root.opening) {
            progressAnim.stop()
            root.progress = 0
        }
        root.shown = true
        root.opening = true
        root.animateTo(1, root.openDuration)
    }

    function close() {
        if (!root.shown)
            return
        const back = progressAnim.running && root.opening
        root.shown = false
        root.opening = false
        root.animateTo(0, Math.round(root.progress * (back ? root.openDuration : root.closeDuration)))
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

    Item {
        id: face

        anchors.fill: parent
        opacity: root.visualOpacity
        layer.enabled: root.animating

        Rectangle {
            anchors.fill: parent
            color: Ui.bg
        }

        Item {
            id: exitCell

            readonly property string touchKind: root.shown ? "exit" : ""

            x: root.gridX - root.gap / 2
            y: root.gridY - root.gap / 2
            width: root.pitch
            height: root.pitch

            Text {
                id: exitText

                x: Ui.px(Ui.jumpExitCx) - exitCell.x - exitText.width / 2
                y: Ui.px(Ui.jumpExitTop + Ui.capHeight * Ui.jumpExit.size) - exitCell.y - exitText.baselineOffset
                text: "exit"
                color: Ui.white(root.exitFocused ? Ui.jumpExit.alpha : Ui.jumpExit.alphaDim)
                font: Ui.font(Ui.jumpExit)
            }
        }

        Repeater {
            id: cells

            model: root.letters

            delegate: Item {
                id: cell

                required property int index
                required property var modelData
                readonly property bool available: root.has(cell.modelData)
                readonly property string touchKind: root.shown && cell.available ? "letter" : ""
                readonly property int touchIndex: cell.index
                property alias tile: tile

                x: root.cellX(cell.index) - root.gap / 2
                y: root.cellY(cell.index) - root.gap / 2
                width: root.pitch
                height: root.pitch

                LetterTile {
                    id: tile

                    readonly property real snapX: Math.round(root.cellX(cell.index))
                    readonly property real snapY: Math.round(root.cellY(cell.index))

                    x: tile.snapX - cell.x
                    y: tile.snapY - cell.y
                    width: Math.round(root.tile)
                    height: Math.round(root.tile)
                    letter: cell.modelData
                    empty: !cell.available
                    focused: cell.available && root.focusIndex === cell.index
                    pressed: tile.focused && root.pressed
                    animated: !root.screenOff
                }
            }
        }
    }
}
