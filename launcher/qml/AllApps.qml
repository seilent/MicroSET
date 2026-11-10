pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

Item {
    id: page

    required property QtObject launcher
    property alias focusIndex: list.focusIndex
    property bool pressed: false

    readonly property var rows: page.launcher === null ? [] : page.launcher.allApps
    readonly property var favoriteIds: page.launcher === null ? [] : page.launcher.favoriteIds
    signal sheetRequested(string id, string name, bool flatpak, bool starred)

    objectName: "apps"
    anchors.fill: parent

    onRowsChanged: list.snapTo(list.clampIndex(list.focusIndex))

    function move(delta: int): bool {
        return list.moveFocus(delta)
    }

    function movePage(direction: int): bool {
        return list.pageFocus(direction)
    }

    function focusedRow(): var {
        return page.rows[list.focusIndex] === undefined ? null : page.rows[list.focusIndex]
    }

    function letterOf(name: string): string {
        const first = name.length > 0 ? name.charAt(0).toLowerCase() : ""
        return jump.letters.indexOf(first) >= 0 ? first : "#"
    }

    function availableLetters(): var {
        const out = []
        for (const row of page.rows) {
            const letter = page.letterOf(row.name)
            if (out.indexOf(letter) < 0)
                out.push(letter)
        }
        return out
    }

    function openJump(): bool {
        if (page.rows.length === 0)
            return false
        const row = page.focusedRow()
        jump.open(page.availableLetters(), row === null ? "" : page.letterOf(row.name))
        return true
    }

    function jumpTo(letter: string): void {
        for (let i = 0; i < page.rows.length; i++) {
            if (page.letterOf(page.rows[i].name) !== letter)
                continue
            list.snapTo(i)
            return
        }
    }

    function activate(): bool {
        const row = page.focusedRow()
        if (row === null || page.launcher === null)
            return false
        page.launcher.launchApp(row.id)
        return true
    }

    function openSheet(): bool {
        const row = page.focusedRow()
        if (row === null)
            return false
        page.sheetRequested(row.id, row.name, row.flatpak !== "", page.favoriteIds.indexOf(row.id) >= 0)
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
        if (jump.shown)
            return jump.handleAction(name, repeat)
        switch (name) {
        case "up":
            return page.move(-1)
        case "down":
            return page.move(1)
        case "left":
            return page.movePage(-1)
        case "right":
            return page.movePage(1)
        case "a":
            if (!repeat)
                page.activate()
            return true
        case "x":
            if (!repeat)
                page.openSheet()
            return true
        case "y":
            if (!repeat && page.launcher !== null)
                page.launcher.openStore()
            return true
        case "b":
            if (!repeat && page.launcher !== null)
                page.launcher.closePage()
            return true
        case "back":
            if (!repeat)
                page.openJump()
            return true
        default:
            return false
        }
    }

    function press(name: string) {
        if (name === "a")
            page.pressed = true
    }

    function release(name: string) {
        if (name === "a")
            page.pressed = false
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (kind === "header")
            return "b"
        if (kind !== "row")
            return ""
        list.touchFocus(index)
        return "a"
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        if (kind !== "row" || page.launcher === null || page.launcher.launching || !page.launcher.content)
            return false
        list.touchFocus(index)
        page.launcher.pulse()
        return page.handleAction("x", false)
    }

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    CroppedHeader {
        id: header

        text: "apps"
        animated: false
    }

    StatusStrip {
        id: strip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        clock: page.launcher === null ? "" : page.launcher.clock
        battery: page.launcher === null ? 100 : page.launcher.batteryPercent
        batteryPresent: page.launcher !== null && page.launcher.batteryPresent
    }

    Text {
        id: emptyText

        objectName: "apps-empty"

        x: Ui.px(Ui.hubEdge)
        anchors.baseline: parent.top
        anchors.baselineOffset: list.y + Ui.px(Ui.qpHintDrop)
        width: Ui.px(Ui.qpHintW)
        wrapMode: Text.WordWrap
        lineHeightMode: Text.FixedHeight
        lineHeight: Ui.px(Ui.qpHintPitch)
        visible: page.rows.length === 0
        text: "No apps installed."
        color: Ui.white(Ui.quickplayHint.alpha)
        font: Ui.font(Ui.quickplayHint)
    }

    FocusList {
        id: list

        objectName: "applist"
        x: 0
        y: Ui.px(Ui.contentTop)
        width: page.width
        height: page.height - list.y
        model: page.rows

        delegate: Item {
            id: row

            required property int index
            required property var modelData
            readonly property bool starred: page.favoriteIds.indexOf(row.modelData.id) >= 0
            readonly property string touchKind: "row"
            readonly property int touchIndex: row.index
            readonly property Item rowItem: listRow

            width: list.width
            height: list.rowPitch

            ListRow {
                id: listRow

                width: row.width
                ring: true
                ringGlyph: "heart"
                ringToggle: true
                ringOn: row.starred
                icon: row.modelData.source
                primary: row.modelData.name
                focused: list.focusIndex === row.index
                pressed: page.pressed && list.focusIndex === row.index
            }
        }
    }

    JumpGrid {
        id: jump

        objectName: "jump"
        anchors.fill: parent
        pressed: page.pressed
        onJumpTo: (letter) => page.jumpTo(letter)
    }
}
