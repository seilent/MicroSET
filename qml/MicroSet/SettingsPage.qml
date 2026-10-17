pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property QtObject model: null
    property string group: ""
    property string title: page.group
    property bool animated: true
    property bool screenOff: false
    property bool editing: false
    property string editKey: ""
    property var editOriginal: null
    property var draft: null
    property var shownValues: ({})
    property var modelRows: page.model === null ? [] : page.model.rows(page.group, page.model.revision)
    readonly property var rows: page.withDraft(page.modelRows, page.editing, page.editKey, page.draft)

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property var rowTops: page.buildTops(page.rows)
    readonly property real contentHeight: page.rows.length === 0 ? 0 : page.rowTops[page.rows.length - 1] + page.rowHeightOf(page.rows[page.rows.length - 1])
    readonly property var focusedRow: page.rows[page.focusIndex] === undefined ? null : page.rows[page.focusIndex]
    readonly property real focusY: page.rowTopOf(page.focusIndex) - flick.contentY
    readonly property real focusHeight: page.focusedRow === null ? 0 : page.rowHeightOf(page.focusedRow)
    readonly property bool scrolling: scrollAnim.running
    property alias header: header
    property alias flick: flick
    property alias focusBar: focusBar
    property alias repeater: repeater

    signal opened(string group)
    signal entrance(real shift, int stagger, int cap, int duration, bool stripCurve, int from)
    signal settle()

    function firstVisible(): int {
        return page.rowAtY(flick.contentY)
    }

    function feather(): void {
        page.entrance(Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true, page.firstVisible())
    }

    function rowHeightOf(r: var): real {
        return Ui.px(r.kind === "label" ? Ui.rowHeight : Ui.settingsPitch)
    }

    function buildTops(list: var): var {
        const tops = []
        let y = 0
        for (const r of list) {
            tops.push(y)
            y += page.rowHeightOf(r)
        }
        return tops
    }

    function rowTopOf(index: int): real {
        const t = page.rowTops[index]
        return t === undefined ? 0 : t
    }

    function indexOfKey(key: string): int {
        for (let i = 0; i < page.rows.length; i++) {
            if (page.rows[i].key === key)
                return i
        }
        return -1
    }

    function isFocusable(index: int): bool {
        const r = page.rows[index]
        return r !== undefined && r !== null && r.kind !== "label"
    }

    function nearestFocusable(from: int, dir: int): int {
        const step = dir >= 0 ? 1 : -1
        for (let i = from; i >= 0 && i < page.rows.length; i += step) {
            if (page.isFocusable(i))
                return i
        }
        return -1
    }

    function maxContentY(): real {
        return Math.max(0, page.contentHeight - flick.height)
    }

    function visibleY(index: int, fromY: real): real {
        const top = page.rowTopOf(index)
        const h = page.rows[index] === undefined ? 0 : page.rowHeightOf(page.rows[index])
        let y = fromY
        if (top < y)
            y = top
        else if (top + h > y + flick.height)
            y = top + h - flick.height
        return Math.max(0, Math.min(page.maxContentY(), y))
    }

    function scrollTo(y: real, duration: int) {
        touchScroller.stop()
        const target = Math.max(0, Math.min(page.maxContentY(), y))
        scrollAnim.stop()
        if (target === flick.contentY)
            return
        if (!page.live || duration <= 0) {
            flick.contentY = target
            return
        }
        scrollAnim.duration = duration
        scrollAnim.from = flick.contentY
        scrollAnim.to = target
        scrollAnim.start()
    }

    function ensureVisible(duration: int) {
        const base = scrollAnim.running ? scrollAnim.to : flick.contentY
        page.scrollTo(page.visibleY(page.focusIndex, base), duration)
    }

    function completeScroll() {
        if (scrollAnim.running)
            scrollAnim.complete()
    }

    function moveRow(delta: int): bool {
        const target = page.nearestFocusable(page.focusIndex + delta, delta)
        if (target < 0 || target === page.focusIndex)
            return false
        page.focusIndex = target
        page.ensureVisible(Motion.scroll)
        return true
    }

    function rowAtY(y: real): int {
        let index = 0
        for (let i = 0; i < page.rows.length; i++) {
            if (page.rowTopOf(i) <= y)
                index = i
        }
        return index
    }

    function pageRow(dir: int): bool {
        if (page.rows.length === 0)
            return false
        const base = scrollAnim.running ? scrollAnim.to : flick.contentY
        const wanted = page.rowTopOf(page.focusIndex) + dir * flick.height
        const index = wanted <= 0 ? 0 : wanted >= page.contentHeight ? page.rows.length - 1 : page.rowAtY(wanted)
        const fixed = page.nearestFocusable(index, dir)
        const target = fixed < 0 ? page.nearestFocusable(index, -dir) : fixed
        if (target < 0 || target === page.focusIndex)
            return false
        page.focusIndex = target
        page.scrollTo(page.visibleY(target, base + dir * flick.height), Motion.page)
        return true
    }

    function withDraft(list: var, editing: bool, key: string, draft: var): var {
        if (!editing || draft === null)
            return list
        return list.map((r) => r.key === key ? Object.assign({}, r, { value: page.model.text(key, draft), current: draft }) : r)
    }

    function step(delta: int): bool {
        const r = page.focusedRow
        if (r === null || page.model === null || r.readOnly)
            return false
        if (page.draft !== null) {
            const next = page.model.next(r.key, page.draft, delta)
            if (next === undefined)
                return false
            page.draft = next
            return true
        }
        if (r.widget === "hue") {
            const hue = Ui.stepHue(r.hue < 0 ? Ui.accentHueDefault : r.hue, delta)
            return page.model.set(r.key, r.swatch !== "" ? page.model.colorFor(hue) : hue)
        }
        return page.model.set(r.key, page.model.next(r.key, r.current, delta))
    }

    function beginEdit(key: string): void {
        if (page.model === null || !page.model.beginEdit(key))
            return
        const r = page.rows[page.indexOfKey(key)]
        page.editOriginal = r.current
        page.draft = r.confirm ? r.current : null
        page.editKey = key
        page.editing = true
    }

    function confirmEdit(): void {
        page.editing = false
        page.editKey = ""
        page.editOriginal = null
        page.draft = null
    }

    function cancelEdit(): void {
        if (!page.editing)
            return
        const key = page.editKey
        const original = page.editOriginal
        const drafted = page.draft !== null
        page.confirmEdit()
        if (!drafted && original !== undefined && original !== null && page.model.available)
            page.model.set(key, original)
    }

    function applyEdit(): void {
        const key = page.editKey
        const draft = page.draft
        page.confirmEdit()
        if (draft !== null)
            page.model.set(key, draft)
    }

    function revertOrConfirmForRotation(): void {
        if (!page.editing)
            return
        if (page.draft !== null)
            page.cancelEdit()
        else
            page.confirmEdit()
    }

    function handleEdit(name: string, repeat: bool): bool {
        switch (name) {
        case "left":
            page.step(-1)
            return true
        case "right":
            page.step(1)
            return true
        case "a":
            if (!repeat)
                page.applyEdit()
            return true
        case "b":
            if (!repeat)
                page.cancelEdit()
            return true
        default:
            return true
        }
    }

    function activate(): bool {
        const r = page.focusedRow
        if (r === null)
            return false
        if (r.group !== undefined) {
            page.opened(r.group)
            return true
        }
        if (r.kind !== "info" && !r.readOnly)
            page.beginEdit(r.key)
        return true
    }

    function fixFocus() {
        if (page.rows.length === 0) {
            page.focusIndex = 0
            return
        }
        if (page.isFocusable(page.focusIndex))
            return
        const target = page.nearestFocusable(Math.min(page.focusIndex, page.rows.length - 1), 1)
        page.focusIndex = target < 0 ? page.nearestFocusable(page.rows.length - 1, -1) : target
    }

    function keepFocusVisible(): void {
        page.completeScroll()
        flick.contentY = page.visibleY(page.focusIndex, flick.contentY)
    }

    function completeAll() {
        page.settle()
        page.completeScroll()
    }

    function back(): bool {
        if (page.stack === null || page.stack.depth < 2)
            return false
        page.stack.pop()
        return true
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (page.editing) {
            if (kind === "header")
                return "b"
            if (kind === "row" && index === page.focusIndex)
                return x < hit.width / 2 ? "left" : "right"
            return "a"
        }
        switch (kind) {
        case "row":
            page.focusIndex = index
            return "a"
        case "header":
            return "b"
        default:
            return ""
        }
    }

    function touchTap(kind: string, index: int, hit: Item) {
        if (!page.editing && kind === "row")
            page.ensureVisible(Motion.scroll)
    }

    function handleAction(name: string, repeat: bool): bool {
        if (page.editing)
            return page.handleEdit(name, repeat)
        switch (name) {
        case "up":
            return page.moveRow(-1)
        case "down":
            return page.moveRow(1)
        case "lt":
            return page.pageRow(-1)
        case "rt":
            return page.pageRow(1)
        case "a":
            if (!repeat)
                page.activate()
            return true
        case "b":
            if (repeat)
                return false
            return page.back()
        default:
            return false
        }
    }

    onRowsChanged: {
        page.fixFocus()
        const r = page.rows[page.focusIndex]
        if (page.editing && (r === undefined || r === null || r.key !== page.editKey || r.readOnly))
            page.confirmEdit()
    }

    onActiveChanged: {
        if (!page.active)
            page.confirmEdit()
        if (page.model !== null && page.active)
            page.model.enter(page.group)
        else if (page.model !== null)
            page.model.leave(page.group)
    }

    onScreenOffChanged: {
        if (page.screenOff)
            page.confirmEdit()
    }

    onEntered: (first, kind) => {
        if (page.arriving)
            page.feather()
        if (page.model !== null && page.group === page.model.updatesGroup)
            page.model.reloadUpdates()
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    Component.onCompleted: {
        page.focusIndex = Math.max(0, page.nearestFocusable(0, 1))
    }

    Component.onDestruction: repeater.model = []

    Connections {
        target: Ui

        function onTurnChanged() {
            page.revertOrConfirmForRotation()
        }
    }

    Connections {
        target: page.model
        ignoreUnknownSignals: true

        function onAvailableChanged() {
            if (!page.model.available)
                page.confirmEdit()
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    Flickable {
        id: flick

        readonly property bool touchScroll: true
        readonly property bool touchScrollEnabled: !page.editing
        property alias touch: touchScroller

        function stopScroll(): void {
            scrollAnim.stop()
        }

        function clampFocusToView(): void {
            const r = page.focusedRow
            if (r === null)
                return
            const top = flick.contentY
            const bottom = flick.contentY + flick.height
            const rowTop = page.rowTopOf(page.focusIndex)
            if (rowTop >= top - 0.5 && rowTop + page.rowHeightOf(r) <= bottom + 0.5)
                return
            let target = -1
            if (rowTop < top) {
                let i = page.rowAtY(top)
                if (page.rowTopOf(i) < top - 0.5)
                    i++
                target = page.nearestFocusable(i, 1)
            } else {
                let i = page.rowAtY(bottom)
                if (page.rowTopOf(i) + page.rowHeightOf(page.rows[i]) > bottom + 0.5)
                    i--
                target = page.nearestFocusable(i, -1)
            }
            if (target >= 0)
                page.focusIndex = target
        }

        x: 0
        y: Ui.px(Ui.contentTop)
        width: page.width
        height: page.height - flick.y
        contentWidth: flick.width
        contentHeight: page.contentHeight
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Repeater {
            id: repeater

            model: page.rows

            delegate: SettingsRow {
                page: page
            }
        }

        TouchScroll {
            id: touchScroller

            flick: page.flick
            limit: page.maxContentY
            onMoved: flick.clampFocusToView()
        }
    }

    FocusBar {
        id: focusBar

        visible: page.focusedRow !== null
        rowY: flick.y + page.focusY
        rowHeight: page.focusHeight
        opacity: page.focusOpacity
        animated: page.live
    }

    CroppedHeader {
        id: header

        text: page.title
        animated: page.live
    }

    NumberAnimation {
        id: scrollAnim

        target: flick
        property: "contentY"
        easing.type: Motion.easeIn
    }
}
