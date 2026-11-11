pragma ComponentBehavior: Bound
import QtQuick
import MicroSet

Window {
    id: root

    required property QtObject launcher
    property QtObject pad: null
    property QtObject accent: null
    property bool shotAccentOff: false
    readonly property int accentHue: root.accent !== null && root.accent.hue >= 0 ? root.accent.hue : Ui.accentHueDefault
    readonly property bool focusHidden: confirm.shown || power.shown
    property string appId: ""
    property string appName: ""

    readonly property string page: root.launcher === null ? "home" : String(root.launcher.page)

    function openAppSheet(id: string, name: string, flatpak: bool, starred: bool): void {
        root.appId = id
        root.appName = name
        const verb = starred ? "remove from favorites" : "add to favorites"
        sheet.title = name
        sheet.items = flatpak ? [verb, "uninstall"] : [verb]
        sheet.open()
    }

    function sheetPicked(index: int): void {
        const id = root.appId
        const name = root.appName
        sheet.close()
        if (index === 0) {
            if (root.launcher !== null)
                root.launcher.toggleFavorite(id)
            return
        }
        if (index === 1)
            root.askUninstall(id, name)
    }

    function askUninstall(id: string, name: string): void {
        root.appId = id
        confirm.title = "uninstall " + name + "?"
        confirm.items = ["uninstall", "keep"]
        confirm.open()
    }

    function confirmPicked(index: int): void {
        const id = root.appId
        confirm.close()
        if (index === 0 && root.launcher !== null)
            root.launcher.uninstallApp(id)
    }

    visible: false
    color: Ui.bg
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "microset-launcher"

    Component.onCompleted: {
        Ui.accentHueColor = Qt.binding(() => Ui.accentForHue(root.accentHue))
        Ui.accentHueBright = Qt.binding(() => Ui.brightForHue(root.accentHue))
        Ui.accentOff = Qt.binding(() => root.shotAccentOff || (root.accent !== null && root.accent.off === true))
    }

    Item {
        id: home

        property QtObject launcher: root.launcher

        readonly property var windowList: home.launcher === null ? [] : home.launcher.windows
        readonly property var appList: home.launcher === null ? [] : home.launcher.apps
        readonly property int columns: home.launcher === null || home.launcher.columns < 1 ? 1 : home.launcher.columns
        readonly property bool hasRunning: home.windowList.length > 0
        readonly property int gridBase: home.hasRunning ? 1 : 0
        readonly property real gridTop: home.hasRunning
            ? Ui.px(Ui.pageContentTop) + Ui.px(Ui.tileCellH + Ui.gutter) : Ui.px(Ui.pageContentTop)
        readonly property real rowPitch: Ui.px(Ui.tileCellH + Ui.gutter)
        readonly property real cellPitch: Ui.px(Ui.tileS + Ui.gutter)
        readonly property bool empty: home.launcher !== null && home.launcher.favoriteCount === 0
        readonly property bool power: home.launcher !== null && home.launcher.power
        readonly property int focusRow: home.launcher === null ? 0 : home.launcher.focusRow
        readonly property int focusIndex: home.launcher === null ? 0 : home.launcher.focusIndex
        readonly property int gridIndex: home.hasRunning ? home.focusRow - 1 : home.focusRow
        readonly property int border: Ui.px(Ui.focusBorder)
        property int revision: 0

        function gridRowOf(index: int): int {
            return home.gridBase + Math.floor(index / home.columns)
        }

        function gridColOf(index: int): int {
            return index % home.columns
        }

        function activate(index: int): void {
            if (home.launcher !== null)
                home.launcher.focusGridAt(index)
        }

        function activateWindow(index: int): void {
            if (home.launcher !== null)
                home.launcher.focusRunningAt(index)
        }

        function focusedTile(): var {
            if (home.hasRunning && home.focusRow === 0)
                return null
            const index = home.gridIndex * home.columns + home.focusIndex
            return index < 0 || index >= home.appList.length ? null : home.appList[index]
        }

        function touchPress(kind: string, index: int, x: real, hit: Item): string {
            if (home.launcher === null)
                return ""
            if (kind === "tile") {
                home.activate(index)
                return "a"
            }
            if (kind === "running") {
                home.activateWindow(index)
                return "a"
            }
            if (kind === "header")
                return "b"
            return ""
        }

        function touchContext(kind: string, index: int, hit: Item): bool {
            if (home.launcher === null || home.launcher.launching || !home.launcher.content || kind !== "tile")
                return false
            home.activate(index)
            home.launcher.pulse()
            return home.launcher.handleAction("x", false)
        }

        width: root.width
        height: root.height
        objectName: "home"
        visible: home.launcher !== null && home.launcher.content && root.page === "home"

        onWidthChanged: home.syncColumns()
        onHeightChanged: home.syncColumns()

        function syncColumns(): void {
            Ui.setSurface(home.width, home.height)
            if (home.launcher !== null)
                home.launcher.columns = Ui.gridCols
        }

        Component.onCompleted: home.syncColumns()

        Rectangle {
            anchors.fill: parent
            color: Ui.bg
        }

        CroppedHeader {
            id: header

            text: "apps"
        }

        StatusStrip {
            id: strip

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            clock: home.launcher === null ? "" : home.launcher.clock
            battery: home.launcher === null ? 100 : home.launcher.batteryPercent
            batteryPresent: home.launcher !== null && home.launcher.batteryPresent

            Text {
                id: ssidText

                anchors.verticalCenter: strip.row.verticalCenter
                anchors.right: strip.row.left
                anchors.rightMargin: Ui.px(Ui.statusGap)
                visible: home.launcher !== null && home.launcher.wifi
                text: home.launcher === null ? "" : home.launcher.ssid
                elide: Text.ElideRight
                width: Math.min(implicitWidth, Math.max(0, home.width / 2 - Ui.px(Ui.edge)))
                color: Ui.white(Ui.time.alpha)
                font: Ui.font(Ui.time)
            }
        }

        Text {
            id: emptyText

            objectName: "empty"
            x: Ui.px(Ui.hubEdge)
            anchors.baseline: parent.top
            anchors.baselineOffset: home.gridTop + home.rowPitch + Ui.px(Ui.qpHintDrop)
            width: Ui.px(Ui.qpHintW)
            wrapMode: Text.WordWrap
            lineHeightMode: Text.FixedHeight
            lineHeight: Ui.px(Ui.qpHintPitch)
            visible: home.empty && !home.power
            text: "Press X in all apps to pin an app here."
            color: Ui.white(Ui.quickplayHint.alpha)
            font: Ui.font(Ui.quickplayHint)
        }

        GridView {
            id: grid

            readonly property int cellH: home.rowPitch

            objectName: "grid"
            x: Ui.px(Ui.hubEdge) - home.border
            y: home.gridTop - home.border
            width: home.columns * home.cellPitch + 2 * home.border
            height: Math.max(0, home.height - home.gridTop)
            leftMargin: home.border
            topMargin: home.border
            rightMargin: home.border
            bottomMargin: home.border
            cellWidth: home.cellPitch
            cellHeight: grid.cellH
            clip: true
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            model: home.appList

            property bool touchScroll: true
            property bool touchScrollEnabled: true
            property alias touch: gridScroll

            TouchScroll {
                id: gridScroll

                flick: grid
            }

            onContentHeightChanged: home.scrollToFocus()

            delegate: Tile {
                id: cell

                required property int index
                required property var modelData

                readonly property bool endTile: cell.modelData.end !== undefined && cell.modelData.end !== ""
                readonly property string touchKind: "tile"
                readonly property int touchIndex: cell.index

                objectName: cell.endTile ? "end-" + cell.modelData.end : "tile-" + cell.index
                size: Ui.tileS
                source: cell.modelData.source
                title: cell.modelData.name
                focused: !root.focusHidden && home.focusRow === home.gridRowOf(cell.index)
                    && home.focusIndex === home.gridColOf(cell.index)
                captioned: true
            }
        }

        ListView {
            id: runningRow

            objectName: "running"
            x: Ui.px(Ui.hubEdge) - home.border
            y: Ui.px(Ui.pageContentTop) - home.border
            width: home.width - 2 * Ui.px(Ui.hubEdge) + 2 * home.border
            height: Ui.px(Ui.tileCellH) + home.border
            orientation: ListView.Horizontal
            leftMargin: home.border
            rightMargin: home.border
            interactive: false
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            visible: home.hasRunning
            model: home.windowList

            property bool touchScroll: true
            property bool touchScrollEnabled: true
            property alias touch: runningScroll

            TouchScroll {
                id: runningScroll

                flick: runningRow
            }

            onContentWidthChanged: home.scrollToFocus()

            delegate: Item {
                id: windowCell

                required property int index
                required property var modelData

                width: home.cellPitch
                height: Ui.px(Ui.tileCellH) + home.border

                Tile {
                    id: windowTile

                    y: home.border

                    readonly property string touchKind: "running"
                    readonly property int touchIndex: windowCell.index

                    objectName: "running-" + windowCell.index
                    size: Ui.tileS
                    source: home.revision >= 0 && home.launcher !== null
                        ? home.launcher.windowIcon(windowCell.index) : ""
                    title: home.revision >= 0 && home.launcher !== null
                        ? home.launcher.windowName(windowCell.index) : ""
                    focused: !root.focusHidden && home.focusRow === 0 && home.focusIndex === windowCell.index
                    captioned: true
                }
            }
        }

        function scrollToFocus(): void {
            if (home.power)
                return
            if (home.hasRunning && home.focusRow === 0) {
                home.scrollRunningToFocus()
                return
            }
            grid.forceLayout()
            const row = Math.max(0, home.gridIndex)
            const top = row * home.rowPitch - grid.topMargin
            const low = top - (grid.height - grid.topMargin - grid.bottomMargin - home.rowPitch)
            const min = -grid.topMargin
            const max = Math.max(min, grid.contentHeight + grid.bottomMargin - grid.height)
            let y = grid.contentY
            if (top < y)
                y = top
            else if (y < low)
                y = low
            grid.contentY = Math.max(min, Math.min(max, y))
        }

        function scrollRunningToFocus(): void {
            if (!home.hasRunning || home.focusRow !== 0)
                return
            const cell = home.cellPitch
            const left = home.focusIndex * cell - runningRow.leftMargin
            const right = left + cell
            const min = -runningRow.leftMargin
            const max = Math.max(min, runningRow.contentWidth + runningRow.rightMargin - runningRow.width)
            let x = runningRow.contentX
            if (left < x)
                x = left
            else if (x + runningRow.width < right)
                x = right - runningRow.width
            runningRow.contentX = Math.max(min, Math.min(max, x))
        }

        function openFocusedSheet(): void {
            const tile = home.focusedTile()
            if (tile === null || tile.id === "")
                return
            root.openAppSheet(tile.id, tile.name, tile.flatpak !== "", true)
        }

        Connections {
            target: home.launcher

            function onAppsChanged() {
                home.revision++
            }

            function onWindowsChanged() {
                home.revision++
            }

            function onFocusChanged() {
                home.scrollToFocus()
            }

            function onPowerChanged() {
                if (home.power)
                    power.open()
                else
                    power.close()
            }

            function onSheetRequested() {
                home.openFocusedSheet()
            }
        }
    }

    AllApps {
        id: apps

        launcher: root.launcher
        focusHidden: root.focusHidden
        visible: root.launcher !== null && root.launcher.content && root.page === "allapps"
        onSheetRequested: (id, name, flatpak, starred) => root.openAppSheet(id, name, flatpak, starred)
    }

    PowerPage {
        id: power

        objectName: "power"
        anchors.fill: parent
        onPicked: (verb) => {
            if (root.launcher !== null)
                root.launcher.runPower(verb)
        }
        onDismissed: {
            if (root.launcher !== null)
                root.launcher.closePower()
        }
    }

    Sheet {
        id: sheet

        objectName: "sheet"
        anchors.fill: parent
        onAccepted: (index) => root.sheetPicked(index)
    }

    Dialog {
        id: confirm

        objectName: "confirm"
        anchors.fill: parent
        onAccepted: (index) => root.confirmPicked(index)
    }

    Toast {
        id: toast

        objectName: "toast"
        x: 0
        width: root.width
        overlayUp: sheet.shown
        topPlace: sheet.shown
    }

    Connections {
        target: root.launcher

        function onLaunchFailed(name, message) {
            toast.show(name, message)
        }

        function onUninstallFailed(name, message) {
            toast.show(name, message)
        }
    }

    TouchLayer {
        id: touch

        anchors.fill: parent
        enabled: root.launcher !== null && root.launcher.content
        dialogs: [confirm, sheet, power]
        router: router
    }

    LauncherRouter {
        id: router

        launcher: root.launcher
        pad: root.pad
        home: home
        allApps: apps
        sheet: sheet
        dialog: confirm
        powerPage: power
    }
}
