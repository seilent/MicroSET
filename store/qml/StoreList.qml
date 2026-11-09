pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

Page {
    id: page

    required property QtObject store
    property Item keyboard: null

    readonly property var rows: page.store === null ? [] : page.store.rows
    readonly property bool offline: page.store !== null && page.store.offline
    readonly property string emptyText: page.offline ? "The store is offline." : "No apps found."

    signal opened(string id)

    objectName: "storelist"
    list: rowList

    onRowsChanged: rowList.snapTo(rowList.clampIndex(rowList.focusIndex))

    function move(delta: int): bool {
        return rowList.moveFocus(delta)
    }

    function movePage(direction: int): bool {
        return rowList.pageFocus(direction)
    }

    function focusedRow(): var {
        return page.rows[rowList.focusIndex] === undefined ? null : page.rows[rowList.focusIndex]
    }

    function iconUrl(row: var): string {
        return row.iconUrl === undefined ? "" : String(row.iconUrl)
    }

    function activate(): bool {
        const row = page.focusedRow()
        if (row === null || page.store === null)
            return false
        page.opened(String(row.id))
        return true
    }

    function openSearch(): bool {
        if (page.keyboard === null || page.store === null)
            return false
        page.keyboard.open({ title: "search", text: String(page.store.search), kind: "text" })
        return true
    }

    function clearSearch(): bool {
        if (page.store === null || page.store.search === "")
            return false
        page.store.search = ""
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
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
        case "y":
            if (!repeat)
                page.openSearch()
            return true
        case "back":
            if (!repeat)
                page.clearSearch()
            return true
        case "b":
            if (!repeat && page.store !== null)
                page.store.quit()
            return true
        default:
            return false
        }
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (kind === "header")
            return "b"
        if (kind !== "row")
            return ""
        rowList.touchFocus(index)
        return "a"
    }

    function touchTap(kind: string, index: int, hit: Item) {
        if (kind === "row")
            rowList.ensureVisible(Motion.scroll)
    }

    PageSurface {
        anchors.fill: parent
    }

    CroppedHeader {
        id: header

        text: "store"
        animated: false
    }

    StatusStrip {
        id: strip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        clock: page.store === null ? "" : page.store.clock
        battery: page.store === null ? 100 : page.store.batteryPercent
        batteryPresent: page.store !== null && page.store.batteryPresent

        Text {
            id: ssidText

            anchors.verticalCenter: strip.row.verticalCenter
            anchors.right: strip.row.left
            anchors.rightMargin: Ui.px(Ui.statusGap)
            visible: page.store !== null && page.store.wifi
            text: page.store === null ? "" : page.store.ssid
            elide: Text.ElideRight
            width: Math.min(implicitWidth, Math.max(0, page.width / 2 - Ui.px(Ui.edge)))
            color: Ui.white(Ui.time.alpha)
            font: Ui.font(Ui.time)
        }
    }

    Text {
        id: emptyText

        objectName: "store-empty"

        x: Ui.px(Ui.hubEdge)
        anchors.baseline: parent.top
        anchors.baselineOffset: rowList.y + Ui.px(Ui.qpHintDrop)
        width: Ui.px(Ui.qpHintW)
        wrapMode: Text.WordWrap
        lineHeightMode: Text.FixedHeight
        lineHeight: Ui.px(Ui.qpHintPitch)
        visible: page.rows.length === 0
        text: page.emptyText
        color: Ui.white(Ui.quickplayHint.alpha)
        font: Ui.font(Ui.quickplayHint)
    }

    FocusList {
        id: rowList

        objectName: "storelist-rows"
        x: 0
        y: Ui.px(Ui.contentTop)
        width: page.width
        height: page.height - rowList.y
        model: page.rows

        delegate: Item {
            id: row

            required property int index
            required property var modelData
            readonly property string touchKind: "row"
            readonly property int touchIndex: row.index
            readonly property string icon: page.iconUrl(row.modelData)
            readonly property bool installed: row.modelData.installed === true

            width: rowList.width
            height: rowList.rowPitch

            ListRow {
                id: listRow

                width: row.width
                primary: String(row.modelData.name)
                secondary: String(row.modelData.summary)
                trailing: row.installed ? "installed" : ""
                icon: row.icon
                focused: rowList.focusIndex === row.index
                pressed: page.pressed && rowList.focusIndex === row.index

                LetterTile {
                    visible: row.icon === "" && !row.installed
                    size: listRow.iconSize
                    letter: row.modelData.letter === undefined ? "" : String(row.modelData.letter)
                    x: listRow.iconItem.x
                    y: listRow.iconItem.y
                }
            }
        }
    }

    FocusBar {
        id: focusBar

        visible: page.rows.length > 0
        x: rowList.x + Ui.px(Ui.hubEdge - Ui.focusBarOffset)
        rowY: rowList.y + rowList.focusY
        rowHeight: rowList.rowPitch
        opacity: page.focusOpacity
        animated: false
    }
}
