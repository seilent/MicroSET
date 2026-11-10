pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

Page {
    id: page

    required property QtObject store

    readonly property var entry: page.store === null ? ({}) : page.store.current
    readonly property var rows: page.buildRows()
    readonly property bool busy: page.store !== null && page.store.busy
    readonly property bool queued: page.store !== null && page.store.queued
    readonly property int percent: page.store === null ? -1 : page.store.percent
    readonly property string actionLabel: page.store === null ? "" : page.store.action
    readonly property string job: page.store === null ? "" : page.store.kind
    readonly property bool installed: page.entry.installed === true
    readonly property string name: page.entry.name === undefined ? "" : String(page.entry.name)
    readonly property string developer: page.entry.developer === undefined ? "" : String(page.entry.developer)
    readonly property url artUrl: page.entry.iconLargeUrl === undefined ? "" : String(page.entry.iconLargeUrl)
    readonly property real artX: Ui.px(Ui.albumArtX)
    readonly property real artY: Ui.px(Ui.albumArtTop)
    readonly property real artSide: Ui.px(Ui.tileL)
    readonly property real textX: Ui.landscape ? Ui.px(Ui.npColumnRight) : Ui.px(Ui.edge)
    readonly property real textWidth: page.width - page.textX - Ui.px(Ui.edge)
    readonly property real artistBaseline: Ui.px(Ui.landscape ? Ui.npArtistBaselineLandscape : Ui.npTextTop)
    readonly property real titleBaseline: Ui.px(Ui.landscape ? Ui.npAlbumBaselineLandscape : Ui.npAlbumBaseline)
    readonly property real listX: Ui.px(Ui.detailListX)
    readonly property real listY: Ui.px(Ui.albumRowsTop)

    signal uninstallRequested(string name)

    objectName: "storedetail"
    list: rowList

    onRowsChanged: rowList.snapTo(rowList.clampIndex(rowList.focusIndex))

    function buildRows(): var {
        const out = []
        if (page.store === null || page.entry.id === undefined)
            return out
        out.push({ key: "action", title: page.actionLabel, value: "", action: true })
        if (page.developer !== "")
            out.push({ key: "developer", title: "developer", value: page.developer })
        const summary = page.entry.summary === undefined ? "" : String(page.entry.summary)
        if (summary !== "")
            out.push({ key: "summary", title: "summary", value: summary })
        const size = page.sizeText(page.entry.installedSize === undefined ? 0 : Number(page.entry.installedSize))
        if (size !== "")
            out.push({ key: "size", title: "size", value: size })
        if (page.busy)
            out.push({ key: "progress", title: page.progressLabel(),
                       value: page.queued || page.percent < 0 ? "" : page.percent + "%" })
        return out
    }

    function progressLabel(): string {
        if (page.queued)
            return "queued"
        return page.job === "uninstall" ? "uninstalling" : "installing"
    }

    function sizeText(bytes: real): string {
        if (!(bytes > 0))
            return ""
        const mb = bytes / 1e6
        if (mb >= 1000)
            return (mb / 1000).toFixed(1) + " GB"
        return (mb >= 10 ? Math.round(mb).toString() : mb.toFixed(1)) + " MB"
    }

    function activate(): bool {
        if (page.store === null || page.entry.id === undefined)
            return false
        if (rowList.focusIndex !== 0)
            return false
        page.store.activate()
        return true
    }

    function back(): bool {
        if (page.stack === null || page.stack.depth < 2)
            return false
        if (page.store !== null)
            page.store.back()
        page.stack.pop()
        return true
    }

    function askUninstall(): bool {
        if (page.store === null || !page.installed)
            return false
        page.uninstallRequested(page.name)
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
        if (page.store === null)
            return false
        switch (name) {
        case "up":
            return rowList.moveFocus(-1)
        case "down":
            return rowList.moveFocus(1)
        case "a":
            if (!repeat)
                page.activate()
            return true
        case "x":
            if (!repeat)
                page.askUninstall()
            return true
        case "b":
            if (!repeat)
                page.back()
            return true
        default:
            return false
        }
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (kind === "header" || kind === "back")
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

    function touchContext(kind: string, index: int, hit: Item): bool {
        if (kind !== "row" || !page.installed)
            return false
        rowList.touchFocus(index)
        return page.handleAction("x", false)
    }

    PageSurface {
        anchors.fill: parent
    }

    Item {
        id: backRingClip

        readonly property string touchKind: "back"

        width: Ui.px(Ui.backRingX + Ui.backRing)
        height: Ui.px(Ui.backRingY + Ui.backRing)
        clip: true

        Ring {
            id: backRing

            x: Ui.px(Ui.backRingX)
            y: Ui.px(Ui.backRingY)
            size: Ui.backRing
            focused: true
            glyph: "back"
            animated: false
        }
    }

    StatusStrip {
        id: strip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        clock: page.store === null ? "" : page.store.clock
        battery: page.store === null ? 100 : page.store.batteryPercent
        batteryPresent: page.store !== null && page.store.batteryPresent
    }

    Tile {
        id: art

        x: page.artX
        y: page.artY
        size: Ui.tileL
        source: page.artUrl
        captioned: false
        animated: false
    }

    Ring {
        id: progressRing

        objectName: "store-progress"
        visible: page.busy
        x: art.x + art.width - progressRing.width - Ui.px(Ui.detailRingInset)
        y: art.y + Ui.px(Ui.detailRingInset)
        size: Ui.detailRing
        glyph: ""
        focused: true
        animated: false
        arcSweep: page.percent <= 0 ? 0 : page.percent * 3.6
    }

    Text {
        id: developerText

        x: page.textX
        y: page.artistBaseline - developerText.baselineOffset
        width: page.textWidth
        text: page.developer
        color: Ui.white(Ui.npArtist.alpha)
        font: Ui.font(Ui.npArtist)
        clip: true
    }

    Text {
        id: nameText

        x: page.textX
        y: page.titleBaseline - nameText.baselineOffset
        width: page.textWidth
        text: page.name
        color: Ui.white(Ui.npAlbum.alpha)
        font: Ui.font(Ui.npAlbum)
        clip: true
    }

    FocusList {
        id: rowList

        objectName: "storedetail-rows"
        x: page.listX
        y: page.listY
        width: page.width - rowList.x
        height: page.height - rowList.y
        model: page.rows

        delegate: Item {
            id: row

            required property int index
            required property var modelData
            readonly property string touchKind: "row"
            readonly property int touchIndex: row.index

            width: rowList.width
            height: rowList.rowPitch

            ListRow {
                id: listRow

                width: row.width
                primary: String(row.modelData.title)
                secondary: String(row.modelData.value)
                bold: row.modelData.action === true
                focused: rowList.focusIndex === row.index
                pressed: page.pressed && rowList.focusIndex === row.index
            }
        }
    }
}
