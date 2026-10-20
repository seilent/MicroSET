pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool shown: false
    property bool screenOff: false
    property bool animated: !root.screenOff
    property string title: ""
    property var items: []
    property int focusIndex: 0
    property int mark: -1
    property bool wrap: false
    property int firstRow: 0
    property real barIndex: 0
    property real progress: 0
    property var stack: []
    readonly property int depth: root.stack.length
    readonly property bool animating: progressAnim.running
    readonly property int duration: Motion.sheet
    readonly property real scrimAlpha: Motion.sheetScrim
    readonly property color scrimColor: Ui.scrimSheet
    readonly property real travel: panel.height
    readonly property real pitch: Ui.px(Ui.settingsPitch)
    readonly property real edge: Ui.px(Ui.edge)
    readonly property real titleHeight: titleText.visible ? titleText.implicitHeight : 0
    readonly property int shownRows: root.items.length === 0 ? 0
        : Math.max(1, Math.min(root.items.length, Math.floor((root.height - 2 * root.edge - root.titleHeight) / root.pitch)))
    readonly property int lastFirstRow: Math.max(0, root.items.length - root.shownRows)
    property alias scrim: scrim
    property alias panel: panel
    property alias bar: bar
    property alias list: list
    property alias window: window
    readonly property string touchKind: root.shown ? "" : "block"

    signal accepted(int index)
    signal rejected()
    signal focusMoved(int index)

    visible: (root.shown || root.progress > 0) && !root.screenOff

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (!root.shown)
            return ""
        switch (kind) {
        case "item":
            root.focusIndex = index
            return "a"
        case "scrim":
            return "b"
        default:
            return ""
        }
    }

    Rectangle {
        id: scrim

        readonly property string touchKind: root.shown ? "scrim" : ""

        anchors.fill: parent
        color: root.scrimColor
        opacity: root.progress
    }

    Rectangle {
        id: panel

        readonly property string touchKind: "block"

        x: 0
        y: root.height - panel.height + (1 - root.progress) * panel.height
        width: root.width
        height: root.edge + root.titleHeight + root.shownRows * root.pitch + root.edge
        color: Ui.bgElevated
        opacity: root.progress

        Text {
            id: titleText

            x: root.edge
            y: root.edge
            visible: root.title !== ""
            text: root.title
            color: Ui.white(Ui.dialogTitle.alpha)
            font: Ui.font(Ui.dialogTitle)
        }

        Flickable {
            id: window

            readonly property bool touchScroll: true
            readonly property bool touchScrollEnabled: root.lastFirstRow > 0
            property alias touch: touchScroller

            x: 0
            y: root.edge + root.titleHeight
            width: panel.width
            height: root.shownRows * root.pitch
            contentWidth: window.width
            contentHeight: root.items.length * root.pitch
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Column {
                id: list

                x: 0
                y: 0
                width: window.width

                Repeater {
                    model: root.items

                    delegate: Item {
                        id: row

                        required property int index
                        required property var modelData
                        readonly property bool isFocused: row.index === root.focusIndex
                        readonly property string touchKind: root.shown ? "item" : ""
                        readonly property int touchIndex: row.index

                        width: list.width
                        height: root.pitch

                        Text {
                            x: root.edge
                            anchors.verticalCenter: parent.verticalCenter
                            text: row.modelData
                            color: row.index === root.mark
                            ? Qt.alpha(Ui.accent, row.isFocused ? Ui.dialogItem.alpha : Ui.dialogItem.alphaDim)
                            : Ui.white(row.isFocused ? Ui.dialogItem.alpha : Ui.dialogItem.alphaDim)
                            font: Ui.font(Ui.dialogItem)

                            Behavior on color {
                                enabled: root.animated
                                ColorAnimation {
                                    duration: Motion.focusAlpha
                                }
                            }
                        }
                    }
                }
            }

            FocusBar {
                id: bar

                x: root.edge - Ui.px(Ui.focusBarOffset)
                rowY: list.y + root.barIndex * root.pitch
                rowHeight: root.pitch
                visible: root.items.length > 0
                animated: false
            }

            TouchScroll {
                id: touchScroller

                flick: window
                limit: () => root.lastFirstRow * root.pitch
                onMoved: root.snapScroll()
            }
        }
    }

    NumberAnimation {
        id: progressAnim

        target: root
        property: "progress"
        duration: Motion.sheet
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
    }

    function animateTo(value: real) {
        progressAnim.stop()
        if (!root.animated || value === root.progress) {
            root.progress = value
            return
        }
        progressAnim.from = root.progress
        progressAnim.to = value
        progressAnim.start()
    }

    function open() {
        root.stack = []
        root.mark = -1
        root.wrap = false
        root.focusIndex = 0
        root.firstRow = 0
        root.shown = true
        root.animateTo(1)
    }

    function rise(): void {
        progressAnim.stop()
        root.progress = 0
        root.animateTo(1)
    }

    function push(title: string, items: var): void {
        root.stack = root.stack.concat([{ title: root.title, items: root.items, focusIndex: root.focusIndex, firstRow: root.firstRow, mark: root.mark, wrap: root.wrap }])
        root.rise()
        root.title = title
        root.items = items
        root.mark = -1
        root.wrap = false
        root.focusIndex = 0
        root.firstRow = 0
        root.barIndex = 0
    }

    function pop(): bool {
        if (root.stack.length === 0)
            return false
        const top = root.stack[root.stack.length - 1]
        root.stack = root.stack.slice(0, root.stack.length - 1)
        root.rise()
        root.title = top.title
        root.items = top.items
        root.mark = top.mark
        root.wrap = top.wrap
        root.firstRow = top.firstRow
        root.focusIndex = top.focusIndex
        root.barIndex = top.focusIndex
        return true
    }

    function keepFocusShown(): void {
        if (root.focusIndex < root.firstRow)
            root.firstRow = root.focusIndex
        else if (root.focusIndex > root.firstRow + root.shownRows - 1)
            root.firstRow = root.focusIndex - root.shownRows + 1
        root.firstRow = Math.max(0, Math.min(root.lastFirstRow, root.firstRow))
    }

    function snapScroll(): void {
        const row = Math.max(0, Math.min(root.lastFirstRow, Math.round(window.contentY / root.pitch)))
        touchScroller.lastWrite = row * root.pitch
        root.firstRow = row
        window.contentY = row * root.pitch
        if (root.focusIndex < row)
            root.focusIndex = row
        else if (root.focusIndex > row + root.shownRows - 1)
            root.focusIndex = row + root.shownRows - 1
    }

    function close() {
        root.stack = []
        root.shown = false
        root.animateTo(0)
    }

    function complete() {
        if (barAnim.running)
            barAnim.complete()
        if (progressAnim.running)
            progressAnim.complete()
    }

    function moveFocus(delta: int, repeat: bool): bool {
        const n = root.items.length
        if (n === 0)
            return false
        let to = root.focusIndex + delta
        if (to < 0 || to >= n)
            to = root.wrap && !repeat && n > 1 ? (to + n) % n : Math.max(0, Math.min(n - 1, to))
        if (to === root.focusIndex)
            return true
        root.focusIndex = to
        if (!repeat)
            root.focusMoved(to)
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
        if (!root.shown)
            return false
        switch (name) {
        case "up":
            return root.moveFocus(-1, repeat)
        case "down":
            return root.moveFocus(1, repeat)
        case "a":
            if (!repeat)
                root.accepted(root.focusIndex)
            return true
        case "b":
        case "start":
        case "back":
            if (!repeat && !root.pop()) {
                root.rejected()
                root.close()
            }
            return true
        default:
            return true
        }
    }

    onAnimatedChanged: {
        if (!root.animated)
            root.complete()
    }

    NumberAnimation {
        id: barAnim

        target: root
        property: "barIndex"
        duration: Motion.focusCursor
        easing.type: Motion.sine
    }

    function moveBar(): void {
        barAnim.stop()
        if (!root.animated || !root.shown || root.animating) {
            root.barIndex = root.focusIndex
            return
        }
        barAnim.from = root.barIndex
        barAnim.to = root.focusIndex
        barAnim.start()
    }

    onFocusIndexChanged: {
        root.keepFocusShown()
        root.moveBar()
    }

    onShownRowsChanged: root.keepFocusShown()

    onFirstRowChanged: window.contentY = root.firstRow * root.pitch

    onPitchChanged: window.contentY = root.firstRow * root.pitch
}
