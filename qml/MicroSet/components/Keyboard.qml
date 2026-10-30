pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool shown: false
    property bool screenOff: false
    property bool animated: !root.screenOff
    property real progress: 0
    property string focusCode: "1"
    property string downCode: ""
    property string petal: ""
    property int petalDigit: -1
    property string pick: "tap"
    property int repeats: 0
    property bool toastShown: false
    property var validator: null
    readonly property bool animating: progressAnim.running
    readonly property bool bare: discard.shown
    readonly property bool toastCovers: root.toastShown && Ui.landscape
    readonly property var keys: entry.keys
    readonly property real edge: Ui.px(Ui.edge)
    readonly property real gutter: Ui.px(Ui.qpGutter)
    readonly property real key: Ui.px(Ui.kbKey)
    readonly property real pitch: root.key + root.gutter
    readonly property real padSize: 4 * root.key + 3 * root.gutter
    readonly property real gap: Ui.px(Ui.kbGap)
    readonly property real titleHeight: titleText.implicitHeight
    readonly property real fieldTop: root.edge + root.titleHeight + root.gap
    readonly property real padX: Ui.landscape ? root.width - root.edge - root.padSize : root.edge
    readonly property real padY: Ui.landscape ? root.edge : root.fieldTop + Ui.px(Ui.kbFieldH) + root.gap
    readonly property real panelHeight: Ui.landscape ? root.height : root.padY + root.padSize + root.edge
    readonly property real panelTop: root.height - root.panelHeight
    readonly property var cells: root.petalCells()
    readonly property string touchKind: root.shown ? "" : "block"
    property alias entry: entry
    property alias panel: panel
    property alias field: field
    property alias discard: discard

    signal done(string text)
    signal canceled()
    signal refused(string title, string reason)

    visible: (root.shown || root.progress > 0) && !root.screenOff

    KeyboardModel {
        id: entry
    }

    function roleFont(role: var, caps: int): font {
        return Qt.font({
            family: Ui.family,
            styleName: role.style,
            pixelSize: Ui.fontPx(role.size),
            letterSpacing: role.tracking * Ui.u,
            capitalization: caps
        })
    }

    function isDigit(code: string): bool {
        return code.length === 1 && code >= "0" && code <= "9"
    }

    function keyAt(col: int, row: int): var {
        for (const k of root.keys) {
            if (col >= k.col && col < k.col + k.cols && row >= k.row && row < k.row + k.rows)
                return k
        }
        return null
    }

    function keyOf(code: string): var {
        for (const k of root.keys) {
            if (k.code === code)
                return k
        }
        return null
    }

    function rectOf(k: var): var {
        return {
            x: root.padX + k.col * root.pitch,
            y: root.padY + k.row * root.pitch,
            w: k.cols * root.key + (k.cols - 1) * root.gutter,
            h: k.rows * root.key + (k.rows - 1) * root.gutter
        }
    }

    function dirOf(dx: real, dy: real): string {
        const slop = Ui.px(Ui.touchSlop)
        if (dx * dx + dy * dy <= slop * slop)
            return "tap"
        if (Math.abs(dy) < Math.abs(dx))
            return dx < 0 ? "left" : "right"
        return dy < 0 ? "up" : "down"
    }

    function petalCells(): var {
        if (root.petal === "" || root.petalDigit < 0)
            return []
        const k = root.keyOf(String(root.petalDigit))
        if (k === null)
            return []
        const r = root.rectOf(k)
        const sx = r.w + root.gutter
        const sy = r.h + root.gutter
        const at = { tap: [0, 0], left: [-sx, 0], up: [0, -sy], right: [sx, 0], down: [0, sy] }
        const out = []
        let x0 = r.x
        let x1 = r.x + r.w
        let y0 = r.y
        let y1 = r.y + r.h
        for (const dir of ["tap", "left", "up", "right", "down"]) {
            const ch = entry.chars[root.petalDigit][dir]
            if (ch === "")
                continue
            const cx = r.x + at[dir][0]
            const cy = r.y + at[dir][1]
            out.push({ x: cx, y: cy, w: r.w, h: r.h, ch: ch, dir: dir })
            x0 = Math.min(x0, cx)
            x1 = Math.max(x1, cx + r.w)
            y0 = Math.min(y0, cy)
            y1 = Math.max(y1, cy + r.h)
        }
        const snap = (over, step) => over > root.edge ? Math.ceil(over / step) * step : 0
        const dx = snap(-x0, sx) - snap(x1 - root.width, sx)
        const dy = snap(-(y0 + root.panelTop), sy) - snap(y1 + root.panelTop - root.height, sy)
        return out.map((c) => ({ x: c.x + dx, y: c.y + dy, w: c.w, h: c.h, ch: c.ch, dir: c.dir }))
    }

    function covered(r: var): bool {
        for (const c of root.cells) {
            if (r.x < c.x + c.w + root.gutter && c.x - root.gutter < r.x + r.w
                    && r.y < c.y + c.h + root.gutter && c.y - root.gutter < r.y + r.h)
                return true
        }
        return false
    }

    NumberAnimation {
        id: progressAnim

        target: root
        property: "progress"
        duration: Motion.sheet
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
    }

    function animateTo(value: real): void {
        progressAnim.stop()
        if (!root.animated || value === root.progress) {
            root.progress = value
            return
        }
        progressAnim.from = root.progress
        progressAnim.to = value
        progressAnim.start()
    }

    function open(request: var): void {
        root.validator = typeof request.validator === "function" ? request.validator : null
        entry.open(request)
        root.cancelTouch()
        root.focusCode = "1"
        if (discard.shown)
            discard.close()
        root.shown = true
        root.animateTo(1)
    }

    function close(): void {
        root.cancelTouch()
        if (discard.shown)
            discard.close()
        root.shown = false
        root.animateTo(0)
    }

    function dismiss(): void {
        if (!root.shown)
            return
        root.close()
        root.canceled()
    }

    function complete(): void {
        if (progressAnim.running)
            progressAnim.complete()
        discard.complete()
    }

    function cancelTouch(): void {
        repeatTimer.stop()
        root.repeats = 0
        root.downCode = ""
        root.petal = ""
        root.petalDigit = -1
        root.pick = "tap"
    }

    function back(): void {
        root.cancelTouch()
        if (!entry.edited) {
            root.close()
            root.canceled()
            return
        }
        discard.open()
        discard.focusIndex = 1
        discard.barIndex = 1
    }

    function finish(): void {
        if (!entry.ready)
            return
        let reason = entry.refusal()
        if (reason === "" && typeof root.validator === "function")
            reason = String(root.validator(entry.text))
        if (reason !== "") {
            root.refused(entry.title, reason)
            return
        }
        const text = entry.text
        root.close()
        root.done(text)
    }

    function pressKey(code: string): void {
        switch (code) {
        case "shift":
            entry.tapShift()
            return
        case "symbols":
            entry.toggleSymbols()
            return
        case "delete":
            entry.erase()
            return
        case "space":
            entry.type(" ")
            return
        case "minus":
            entry.type("-")
            return
        case "point":
            entry.type(".")
            return
        case "cancel":
            root.back()
            return
        case "done":
            root.finish()
            return
        default:
            if (entry.number && root.isDigit(code))
                entry.type(code)
        }
    }

    function commitPetal(): void {
        const digit = root.petalDigit
        const dir = root.pick
        root.petal = ""
        root.petalDigit = -1
        root.pick = "tap"
        entry.commit(digit, dir)
    }

    function stepPick(dir: string): void {
        const opposite = { left: "right", right: "left", up: "down", down: "up" }
        const to = root.pick !== "tap" && opposite[root.pick] === dir ? "tap" : dir
        if (entry.chars[root.petalDigit][to] !== "")
            root.pick = to
    }

    function moveFocus(dir: string): bool {
        const k = root.keyOf(root.focusCode)
        if (k === null)
            return false
        const to = dir === "right" ? root.keyAt(k.col + k.cols, k.row)
            : dir === "left" ? root.keyAt(k.col - 1, k.row)
            : dir === "down" ? root.keyAt(k.col, k.row + k.rows)
            : root.keyAt(k.col, k.row - 1)
        if (to !== null)
            root.focusCode = to.code
        return true
    }

    function activate(): void {
        const code = root.focusCode
        if (root.isDigit(code) && !entry.number) {
            root.petal = "pad"
            root.petalDigit = Number(code)
            root.pick = "tap"
            return
        }
        if (code === "shift") {
            entry.stepShift()
            return
        }
        root.pressKey(code)
    }

    function handleAction(name: string, repeat: bool): bool {
        if (!root.shown)
            return false
        if (discard.shown)
            return discard.handleAction(name, repeat)
        if (root.petal === "pad") {
            switch (name) {
            case "up":
            case "down":
            case "left":
            case "right":
                root.stepPick(name)
                return true
            case "a":
                if (!repeat)
                    root.commitPetal()
                return true
            case "b":
            case "back":
                if (!repeat)
                    root.cancelTouch()
                return true
            default:
                return true
            }
        }
        switch (name) {
        case "up":
        case "down":
        case "left":
        case "right":
            return root.moveFocus(name)
        case "a":
            if (!repeat)
                root.activate()
            return true
        case "x":
            entry.erase()
            return true
        case "start":
            if (!repeat)
                root.finish()
            return true
        case "b":
        case "back":
            if (!repeat)
                root.back()
            return true
        default:
            return true
        }
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (!root.shown)
            return ""
        if (kind === "scrim")
            return "b"
        if (kind !== "key" || index < 0 || index >= root.keys.length)
            return ""
        root.cancelTouch()
        const code = root.keys[index].code
        root.focusCode = code
        root.downCode = code
        if (root.isDigit(code) && !entry.number) {
            root.petal = "touch"
            root.petalDigit = Number(code)
            root.pick = "tap"
        } else if (code === "delete") {
            repeatTimer.interval = Ui.seekHoldMs
            repeatTimer.restart()
        }
        return ""
    }

    function touchMove(kind: string, index: int, dx: real, dy: real, hit: Item): void {
        const dir = root.dirOf(dx, dy)
        if (root.downCode === "delete" && dir !== "tap")
            repeatTimer.stop()
        if (root.petal === "touch")
            root.pick = dir
    }

    function touchLift(kind: string, index: int, dx: real, dy: real, hit: Item): void {
        const code = root.downCode
        const dir = root.dirOf(dx, dy)
        const repeated = root.repeats > 0
        repeatTimer.stop()
        root.repeats = 0
        root.downCode = ""
        if (root.petal === "touch") {
            root.pick = dir
            root.commitPetal()
            return
        }
        if (code === "" || dir !== "tap" || (code === "delete" && repeated))
            return
        root.pressKey(code)
    }

    function touchCancel(kind: string, index: int, hit: Item): void {
        root.cancelTouch()
    }

    Timer {
        id: repeatTimer

        interval: Ui.seekHoldMs
        repeat: true
        onTriggered: {
            entry.erase()
            root.repeats++
            repeatTimer.interval = Ui.kbRepeatMs
        }
    }

    Rectangle {
        id: scrim

        readonly property string touchKind: root.shown ? "scrim" : ""

        anchors.fill: parent
        color: Ui.scrimSheet
        opacity: root.progress
    }

    Rectangle {
        id: panel

        readonly property string touchKind: "block"

        x: 0
        y: root.panelTop + (1 - root.progress) * root.panelHeight
        width: root.width
        height: root.panelHeight
        color: Ui.bgElevated
        opacity: root.progress

        Text {
            id: titleText

            x: root.edge
            y: root.edge
            width: field.width
            text: entry.title
            elide: Text.ElideRight
            color: Ui.white(Ui.dialogTitle.alpha)
            font: Ui.font(Ui.dialogTitle)
            visible: !root.bare && !root.toastCovers
        }

        Rectangle {
            id: field

            readonly property real pad: Ui.px(Ui.kbFieldPad)
            readonly property real cursorW: Ui.px(Ui.kbCursorW)
            readonly property real textRight: field.width - field.pad - (entry.unit !== "" ? root.key : 0)

            x: root.edge
            y: root.fieldTop
            width: (Ui.landscape ? root.padX - root.gap : root.width - root.edge) - root.edge
            height: Ui.px(Ui.kbFieldH)
            color: "transparent"
            border.color: Ui.border
            border.width: Ui.px(Ui.kbFieldBorder)

            Item {
                id: textBox

                x: field.pad
                y: 0
                width: Math.max(0, field.textRight - field.pad)
                height: field.height
                clip: true
                visible: !root.bare

                Text {
                    id: typed

                    x: Math.min(0, textBox.width - field.cursorW - typed.implicitWidth)
                    y: field.pad
                    text: entry.text
                    textFormat: Text.PlainText
                    color: Ui.white(Ui.alphaFull)
                    font: root.roleFont(Ui.listPrimary, Font.MixedCase)
                }

                Text {
                    x: field.cursorW + root.gutter
                    y: field.pad
                    visible: entry.text === ""
                    text: entry.hint
                    textFormat: Text.PlainText
                    color: Ui.white(Ui.alphaLow)
                    font: root.roleFont(Ui.listPrimary, Font.MixedCase)
                }

                Rectangle {
                    x: entry.text === "" ? 0 : typed.x + typed.implicitWidth
                    y: field.pad
                    width: field.cursorW
                    height: typed.implicitHeight
                    color: Ui.accent
                }
            }

            Text {
                x: field.width - field.pad - implicitWidth
                y: field.pad + typed.baselineOffset - baselineOffset
                visible: entry.unit !== "" && !root.bare
                text: entry.unit
                color: Ui.white(Ui.listCount.alpha)
                font: Ui.font(Ui.listCount)
            }
        }

        Repeater {
            model: root.keys

            delegate: Rectangle {
                id: cell

                required property int index
                required property var modelData
                readonly property var box: root.rectOf(cell.modelData)
                readonly property string code: cell.modelData.code
                readonly property bool locked: cell.code === "shift" && entry.shift === 2
                readonly property bool plain: root.bare || root.covered(cell.box)
                readonly property string touchKind: root.shown ? "key" : ""
                readonly property int touchIndex: cell.index
                readonly property bool touchTrack: true

                x: cell.box.x
                y: cell.box.y
                width: cell.box.w
                height: cell.box.h
                color: root.downCode === cell.code || cell.locked ? Ui.tilePressed : Ui.tile

                Text {
                    anchors.centerIn: parent
                    visible: !cell.plain
                    text: entry.labels[cell.code] ?? ""
                    color: cell.locked ? Ui.accent
                        : Ui.white(cell.code === "done" && !entry.ready ? Ui.alphaLow : Ui.alphaFull)
                    font: root.roleFont(Ui.listPrimary, Font.MixedCase)
                }

                Text {
                    x: cell.width - Ui.px(Ui.letterInsetX) - implicitWidth
                    y: cell.height - Ui.px(Ui.letterInsetBaseline) - baselineOffset
                    visible: !cell.plain && root.isDigit(cell.code) && !entry.number
                    text: cell.code
                    color: Ui.white(Ui.sectionLabel.alpha)
                    font: Ui.font(Ui.sectionLabel)
                }

                Rectangle {
                    anchors.fill: parent
                    visible: !cell.plain && root.focusCode === cell.code
                    color: "transparent"
                    border.color: Ui.white(Ui.alphaFull)
                    border.width: Ui.px(Ui.focusBorder)
                }
            }
        }

        Repeater {
            model: root.cells

            delegate: Rectangle {
                id: frame

                required property var modelData

                x: frame.modelData.x - root.gutter
                y: frame.modelData.y - root.gutter
                width: frame.modelData.w + 2 * root.gutter
                height: frame.modelData.h + 2 * root.gutter
                color: Ui.bgElevated
            }
        }

        Repeater {
            model: root.cells

            delegate: Rectangle {
                id: petalCell

                required property var modelData

                x: petalCell.modelData.x
                y: petalCell.modelData.y
                width: petalCell.modelData.w
                height: petalCell.modelData.h
                color: petalCell.modelData.dir === root.pick ? Ui.accent : Ui.tilePressed

                Text {
                    anchors.centerIn: parent
                    text: petalCell.modelData.ch
                    textFormat: Text.PlainText
                    color: Ui.white(Ui.letterGlyph.alpha)
                    font: root.roleFont(Ui.letterGlyph, Font.MixedCase)
                }
            }
        }
    }

    Dialog {
        id: discard

        anchors.fill: parent
        screenOff: root.screenOff
        title: "discard text?"
        items: ["discard", "keep"]
        onAccepted: (index) => {
            discard.close()
            if (index === 0) {
                root.close()
                root.canceled()
            }
        }
    }

    onAnimatedChanged: {
        if (!root.animated)
            root.complete()
    }

    onScreenOffChanged: {
        if (root.screenOff)
            root.cancelTouch()
    }
}
