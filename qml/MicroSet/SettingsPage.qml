pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property QtObject model: null
    property string group: ""
    property string title: page.model === null ? page.group : page.model.pageTitle(page.group, page.model.revision)
    property real headerX: Ui.pageHeaderX
    property bool animated: true
    property bool screenOff: false
    property bool editing: false
    property string editKey: ""
    property var draft: null
    property var shownValues: ({})
    property string focusKey: ""
    property int pressedIndex: -1
    property var modelRows: page.model === null ? [] : page.model.rows(page.group, page.model.revision)
    readonly property bool valueOpen: d.valueKey !== ""
    readonly property bool pickerOpen: page.valueOpen || page.editing || rowSheet.shown || gameSheet.shown
    readonly property string draftKey: page.editing && page.draft !== null ? page.editKey
        : page.valueOpen && d.valueDraft && d.moved ? d.valueKey : ""
    readonly property var draftValue: page.editing ? page.draft : page.valueOpen ? d.valueValues[d.valueSheet.focusIndex] : null
    readonly property var rows: page.withDraft(page.modelRows, page.draftKey, page.draftValue)
    readonly property string valueItems: page.valueOpen && page.model !== null
        ? page.itemsOf(page.model.sheet(d.valueKey), page.model.revision) : ""

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    property var rowTops: page.buildTops(page.rows)
    readonly property real contentHeight: page.rows.length === 0 ? 0 : page.rowTops[page.rows.length - 1] + page.rowHeightOf(page.rows[page.rows.length - 1])
    readonly property var focusedRow: page.rows[page.focusIndex] === undefined ? null : page.rows[page.focusIndex]
    readonly property bool scrolling: scrollAnim.running
    property alias header: header
    property alias flick: flick
    property alias repeater: repeater
    property alias sheet: sheet
    property alias swatchSheet: swatchSheet
    property alias rowSheet: rowSheet
    property alias gameSheet: gameSheet
    property alias confirm: confirm
    property alias adjust: adjust
    readonly property var touchDialogs: [confirm, adjust, gameSheet, rowSheet, swatchSheet, sheet]

    signal opened(string group)
    signal acted(string key)
    signal entrance(real shift, int stagger, int cap, int duration, bool stripCurve, int from)
    signal settle()

    function firstVisible(): int {
        return page.rowAtY(flick.contentY)
    }

    function feather(): void {
        page.entrance(Motion.rowsInShift, Motion.rowsInStagger, Motion.rowsInCap, Motion.rowsIn, true, page.firstVisible())
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

    function keyAtFocus(): string {
        return page.rows[page.focusIndex] === undefined ? "" : page.rows[page.focusIndex].key
    }

    function keyToKeep(): string {
        return page.focusIndex === page.nearestFocusable(0, 1) ? "" : page.keyAtFocus()
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
        if (dropAnim.running)
            page.dropNow()
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
        if (dropAnim.running)
            page.dropNow()
    }

    function panelTop(): real {
        const target = d.valueSheet
        const title = target.title === "" ? 0 : liftTitle.implicitHeight
        const room = target.height - target.statusClear - 2 * target.edge - title - target.leadHeight
        const rows = target.items.length === 0 ? 0
            : Math.max(1, Math.min(target.items.length, Math.floor(room / target.pitch)))
        return target.height - (2 * target.edge + title + target.leadHeight + rows * target.pitch)
    }

    function liftTop(index: int): real {
        const need = flick.y + page.rowTopOf(index) + page.rowHeightOf(page.rows[index]) - page.panelTop()
            + Ui.px(Ui.gutter)
        for (let i = 0; i <= index; i++) {
            if (page.rowTopOf(i) >= need - 0.01)
                return page.rowTopOf(i)
        }
        return -1
    }

    function relift(index: int): void {
        const top = page.liftTop(index)
        if (top < 0) {
            liftAnim.stop()
            page.dropNow()
            d.lifted = false
            d.showValue(d.lifting, d.valueKey)
            return
        }
        flick.bottomMargin = Math.max(0, top - page.maxContentY())
        if (liftAnim.running) {
            liftAnim.stop()
            liftAnim.from = flick.contentY
            liftAnim.to = top
            liftAnim.start()
            return
        }
        flick.contentY = top
    }

    function dropNow(): void {
        dropAnim.stop()
        flick.contentY = Math.min(flick.contentY, page.maxContentY())
        flick.bottomMargin = 0
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

    function withDraft(list: var, key: string, draft: var): var {
        if (key === "" || draft === null || draft === undefined)
            return list
        return list.map((r) => r.key === key ? Object.assign({}, r, { value: page.model.text(key, draft), current: draft }) : r)
    }

    function itemsOf(values: var, revision: int): string {
        return JSON.stringify([values.words === undefined ? [] : values.words,
            values.swatches === undefined ? [] : values.swatches])
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
        return page.model.preview(page.model.next(r.key, r.current, delta))
    }

    function beginEdit(key: string): void {
        if (page.model === null || page.model.busy(key) || !page.model.beginEdit(key))
            return
        const r = page.rows[page.indexOfKey(key)]
        page.draft = r.confirm ? r.current : null
        page.editKey = key
        page.editing = true
        d.session++
    }

    function confirmEdit(): void {
        page.editing = false
        page.editKey = ""
        page.draft = null
    }

    function cancelEdit(): void {
        if (!page.editing)
            return
        page.confirmEdit()
        page.model.cancelPreview()
    }

    function applyEdit(): void {
        const r = page.rows[page.indexOfKey(page.editKey)]
        page.confirmEdit()
        if (r !== undefined)
            page.model.endPreview(r.current)
    }

    function openValue(key: string): void {
        if (page.model === null || page.model.busy(key) || !page.model.beginEdit(key))
            return
        if (dropAnim.running)
            page.dropNow()
        const values = page.model.sheet(key)
        const r = page.rows[page.indexOfKey(key)]
        if (values.words === undefined || values.words.length === 0 || r === undefined) {
            page.model.cancelPreview()
            return
        }
        const swatch = r.pick === "swatch"
        d.valueValues = values.values
        d.valueLeads = values.leads
        d.valueDraft = values.draft
        d.valueItems = page.itemsOf(values, 0)
        d.valueKey = key
        d.swatch = swatch
        d.riseKept = ""
        d.session++
        const target = d.valueSheet
        target.title = r.title
        if (swatch)
            swatchSheet.colors = values.swatches.length > 0 ? values.swatches
                : values.values.map((v) => v < 0 ? Ui.textFull : Ui.accentForHue(v))
        else
            sheet.items = values.words
        d.valueSet = values
        d.lifting = d.session
        const index = page.indexOfKey(key)
        const base = scrollAnim.running ? scrollAnim.to : flick.contentY
        const bottom = flick.y + page.rowTopOf(index) + page.rowHeightOf(r) - base
        const top = page.liftTop(index)
        if (top < 0 || bottom <= page.panelTop() + 0.01) {
            d.showValue(d.session, key)
            return
        }
        touchScroller.stop()
        scrollAnim.stop()
        liftAnim.stop()
        flick.bottomMargin = Math.max(0, top - page.maxContentY())
        d.lifted = true
        if (!page.live) {
            flick.contentY = top
            d.showValue(d.session, key)
            return
        }
        liftAnim.from = flick.contentY
        liftAnim.to = top
        liftAnim.start()
    }

    function endValue(now: bool): void {
        const lifting = d.lifting !== 0
        const lifted = d.lifted
        const moving = d.valueSheet.shown && d.valueSheet.animated
        liftAnim.stop()
        d.lifting = 0
        d.lifted = false
        d.valueKey = ""
        d.riseKept = ""
        d.valueValues = []
        d.moved = false
        if (sheet.shown)
            sheet.close()
        if (swatchSheet.shown)
            swatchSheet.close()
        if (!lifted)
            return
        if (now || lifting || !moving || !page.active || flick.contentY <= page.maxContentY()) {
            page.dropNow()
            return
        }
        dropAnim.from = flick.contentY
        dropAnim.to = page.maxContentY()
        dropAnim.start()
    }

    function previewValue(index: int): void {
        if (!page.valueOpen || d.valueDraft || index < 0 || index >= d.valueValues.length)
            return
        page.model.preview(d.valueValues[index])
    }

    function acceptValue(index: int): void {
        const value = d.valueValues[index]
        const key = d.valueKey
        page.endValue(false)
        if (value === undefined)
            return
        if (key === page.model.accentKey)
            page.model.pickAccent(value)
        else
            page.model.endPreview(value)
    }

    function cancelValue(): void {
        if (!page.valueOpen)
            return
        page.endValue(false)
        page.model.cancelPreview()
    }

    function closePickers(): void {
        page.cancelValue()
        page.cancelEdit()
        page.closeRows()
    }

    function openRows(r: var): void {
        if (page.model === null)
            return
        d.rowKey = r.key
        rowSheet.title = r.title
        rowSheet.rows = Qt.binding(() => page.model === null ? [] : page.model.subRows(d.rowKey, page.model.revision))
        rowSheet.covered = false
        rowSheet.open()
        d.rowFocusKey = rowSheet.entries.length > 0 ? rowSheet.entries[0].key : ""
    }

    function closeRows(): void {
        page.closeGame(false)
        if (rowSheet.shown)
            rowSheet.close()
    }

    function pickRow(index: int): void {
        const r = rowSheet.entries[index]
        if (r === undefined || r.installed === undefined)
            return
        if (r.installed !== true) {
            page.model.toggle(r.key)
            return
        }
        const at = r.value === "on" ? 1 : 0
        d.gameKey = r.key
        d.gameValue = r.value
        gameSheet.title = r.title
        gameSheet.items = ["off", "on"]
        gameSheet.open()
        gameSheet.mark = at
        gameSheet.focusIndex = at
        rowSheet.covered = true
    }

    function pickGame(index: int): void {
        const key = d.gameKey
        const flip = (index === 1) !== (d.gameValue === "on")
        page.closeGame(true)
        if (flip)
            page.model.toggle(key)
    }

    function closeGame(rise: bool): void {
        if (d.gameKey === "")
            return
        const key = d.gameKey
        d.gameKey = ""
        d.gameValue = ""
        if (gameSheet.shown)
            gameSheet.close()
        rowSheet.covered = false
        if (!rise || !rowSheet.shown)
            return
        rowSheet.rise()
        const at = rowSheet.entries.findIndex((e) => e.key === key)
        if (at >= 0)
            rowSheet.focusIndex = at
    }

    function followRows(): void {
        if (!rowSheet.shown)
            return
        if (rowSheet.rows.length === 0) {
            page.closeGame(false)
            rowSheet.close()
            return
        }
        const entries = rowSheet.entries
        const at = entries.findIndex((e) => e.key === d.rowFocusKey)
        rowSheet.focusIndex = at >= 0 ? at : Math.max(0, Math.min(rowSheet.focusIndex, entries.length - 1))
        if (d.gameKey === "")
            return
        const game = entries.find((e) => e.key === d.gameKey)
        if (game === undefined || game.value !== d.gameValue)
            page.closeGame(true)
    }

    function cancelLater(): void {
        const session = d.session
        Qt.callLater(() => {
            if (d.session !== session)
                return
            if (page.valueOpen)
                page.cancelValue()
            else if (page.model !== null)
                page.model.cancelPreview()
        })
    }

    function dismiss(): void {
        const open = page.valueOpen || page.editing
        page.endValue(true)
        page.confirmEdit()
        page.closeRows()
        if (confirm.shown)
            confirm.close()
        if (open && page.model !== null)
            page.model.endPreviews()
    }

    function act(r: var): void {
        if (r.ask === undefined || r.ask === "") {
            page.acted(r.action)
            return
        }
        confirm.action = r.action
        confirm.title = r.ask
        confirm.items = [r.accept, "cancel"]
        confirm.open()
        confirm.focusIndex = confirm.cancelIndex
    }

    function holdRise(name: string, repeat: bool): bool {
        if (repeat)
            return true
        if (name === "b" || name === "back" || name === "start")
            d.riseKept = name
        else if ((name === "up" || name === "down") && d.riseKept === "")
            d.riseKept = name
        return true
    }

    function handleEdit(name: string, repeat: bool): bool {
        switch (name) {
        case "left":
            page.step(-1)
            return true
        case "right":
            page.step(1)
            return true
        case "up":
        case "down":
            if (adjust.shown && !adjust.balance)
                page.step(name === "up" ? 1 : -1)
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
        if (r.action !== undefined && r.action !== "") {
            page.act(r)
            return true
        }
        if (r.expand === true) {
            page.openRows(r)
            return true
        }
        if (r.kind === "info" || r.readOnly)
            return true
        switch (r.pick) {
        case "sheet":
        case "swatch":
            page.openValue(r.key)
            break
        case "adjust":
            page.beginEdit(r.key)
            break
        default:
            break
        }
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
        if (d.lifting !== 0 && page.screenOff) {
            liftAnim.stop()
        } else if (d.lifting !== 0) {
            liftAnim.complete()
            d.showValue(d.lifting, d.valueKey)
            d.valueSheet.complete()
            d.valueSheet.list.forceLayout()
        }
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
        switch (kind) {
        case "row":
            page.pressedIndex = index
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
        if (confirm.shown)
            return confirm.handleAction(name, repeat)
        if (d.lifting !== 0)
            return page.holdRise(name, repeat)
        if (d.valueSheet.shown && page.valueOpen && d.valueSheet.animating)
            return page.holdRise(name, repeat)
        if (d.valueSheet.shown)
            return d.valueSheet.handleAction(name, repeat)
        if (gameSheet.shown)
            return gameSheet.handleAction(name, repeat)
        if (rowSheet.shown)
            return rowSheet.handleAction(name, repeat)
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

    onFocusIndexChanged: page.focusKey = page.keyToKeep()

    onPressedChanged: {
        if (!page.pressed)
            page.pressedIndex = -1
    }

    onRowsChanged: {
        page.rowTops = Qt.binding(() => page.buildTops(page.rows))
        const top = page.focusKey === ""
        const kept = top ? page.nearestFocusable(0, 1) : page.indexOfKey(page.focusKey)
        if (kept >= 0 && kept !== page.focusIndex) {
            page.focusIndex = kept
            if (d.lifting === 0 && !d.lifted) {
                if (top)
                    page.scrollTo(0, 0)
                else
                    page.ensureVisible(0)
            }
        }
        page.fixFocus()
        page.focusKey = page.keyToKeep()
        const r = page.rows[page.focusIndex]
        if (page.editing && (r === undefined || r === null || r.key !== page.editKey || r.readOnly)) {
            page.confirmEdit()
            page.cancelLater()
        }
        if (page.valueOpen) {
            const at = page.indexOfKey(d.valueKey)
            if (at < 0 || page.rows[at].readOnly)
                page.cancelLater()
            else if (d.lifting !== 0 || d.lifted)
                page.relift(at)
        }
        if (confirm.shown && !page.rows.some((r) => r.action === confirm.action))
            confirm.close()
    }

    onValueItemsChanged: {
        if (page.valueOpen && page.valueItems !== d.valueItems)
            page.cancelLater()
    }

    onActiveChanged: {
        if (!page.active)
            page.dismiss()
        if (page.model !== null && page.active)
            page.model.enter(page.group)
        else if (page.model !== null)
            page.model.leave(page.group)
    }

    onScreenOffChanged: {
        if (page.screenOff)
            page.dismiss()
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
        if (page.model !== null)
            page.model.beginVisit(page.group)
    }

    Component.onDestruction: {
        repeater.model = []
        if (page.model !== null)
            page.model.endVisit(page.group)
    }

    Connections {
        target: Ui

        function onTurnChanged() {
            page.dismiss()
        }
    }

    Connections {
        target: page.model
        ignoreUnknownSignals: true

        function onAvailableChanged() {
            if (!page.model.available) {
                page.endValue(true)
                page.confirmEdit()
                page.closeRows()
            }
        }

        function onPreviewEnded(key) {
            if (page.valueOpen && d.valueKey === key)
                page.endValue(false)
            if (page.editing && page.editKey === key)
                page.confirmEdit()
        }
    }

    QtObject {
        id: d

        property string rowKey: ""
        property string rowFocusKey: ""
        property string gameKey: ""
        property string gameValue: ""
        property string valueKey: ""
        property string riseKept: ""
        property var valueValues: []
        property var valueLeads: []
        property bool valueDraft: false
        property bool swatch: false
        readonly property Item valueSheet: d.swatch ? swatchSheet : sheet
        property string valueItems: ""
        property bool moved: false
        property int session: 0
        property int lifting: 0
        property bool lifted: false
        property var valueSet: null

        function showValue(session: int, key: string): void {
            if (session === 0 || d.lifting !== session || d.session !== session || d.valueKey !== key)
                return
            d.lifting = 0
            const target = d.valueSheet
            target.open()
            target.mark = d.valueSet.mark
            target.wrap = d.valueSet.wrap
            target.focusIndex = d.valueSet.focus
            d.moved = false
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
        y: Ui.px(Ui.pageContentTop)
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
            retention: Motion.listFlingRetention
            onMoved: flick.clampFocusToView()
        }
    }

    Text {
        id: liftTitle

        visible: false
        text: d.valueSheet.title
        font: Ui.font(Ui.dialogTitle)
    }

    CroppedHeader {
        id: header

        text: page.title
        penX: page.headerX
        alpha: Ui.pageHeaderSettingsAlpha
    }

    NumberAnimation {
        id: scrollAnim

        target: flick
        property: "contentY"
        easing.type: Motion.easeIn
    }

    NumberAnimation {
        id: liftAnim

        target: flick
        property: "contentY"
        duration: Motion.scroll
        easing.type: Motion.easeIn
        onFinished: d.showValue(d.lifting, d.valueKey)
    }

    NumberAnimation {
        id: dropAnim

        target: flick
        property: "contentY"
        duration: Math.min(Motion.scroll, d.valueSheet.duration)
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
        onFinished: flick.bottomMargin = 0
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        lead: d.valueLeads[sheet.focusIndex] === undefined ? "" : d.valueLeads[sheet.focusIndex]
        keepLead: d.valueLeads.some((note) => note !== "")
        screenOff: page.screenOff
        animated: page.live
        onAccepted: (index) => page.acceptValue(index)
        onRejected: page.cancelValue()
        onFocusMoved: (index) => page.previewValue(index)
        onProgressChanged: {
            if (!sheet.shown && sheet.progress === 0)
                d.valueLeads = []
        }
        onFocusIndexChanged: {
            if (page.valueOpen)
                d.moved = true
        }
        onAnimatingChanged: {
            if (sheet.animating || d.riseKept === "")
                return
            const kept = d.riseKept
            d.riseKept = ""
            Qt.callLater(() => {
                if (sheet.shown && page.valueOpen)
                    sheet.handleAction(kept, false)
            })
        }
    }

    SwatchSheet {
        id: swatchSheet

        anchors.fill: parent
        screenOff: page.screenOff
        animated: page.live
        onAccepted: (index) => page.acceptValue(index)
        onRejected: page.cancelValue()
        onFocusMoved: (index) => page.previewValue(index)
        onFocusIndexChanged: {
            if (page.valueOpen)
                d.moved = true
        }
        onAnimatingChanged: {
            if (swatchSheet.animating || d.riseKept === "")
                return
            const kept = d.riseKept
            d.riseKept = ""
            Qt.callLater(() => {
                if (swatchSheet.shown && page.valueOpen)
                    swatchSheet.handleAction(kept, false)
            })
        }
    }

    RowSheet {
        id: rowSheet

        anchors.fill: parent
        screenOff: page.screenOff
        animated: page.live
        onAccepted: (index) => page.pickRow(index)
        onEntriesChanged: page.followRows()
        onProgressChanged: {
            if (!rowSheet.shown && rowSheet.progress === 0)
                Qt.callLater(() => {
                    if (!rowSheet.shown && rowSheet.progress === 0)
                        rowSheet.rows = []
                })
        }
        onFocusIndexChanged: {
            const r = rowSheet.entries[rowSheet.focusIndex]
            if (r !== undefined)
                d.rowFocusKey = r.key
        }
    }

    Sheet {
        id: gameSheet

        anchors.fill: parent
        screenOff: page.screenOff
        animated: page.live
        onAccepted: (index) => page.pickGame(index)
        onRejected: page.closeGame(true)
    }

    Dialog {
        id: confirm

        readonly property int acceptIndex: 0
        readonly property int cancelIndex: 1
        property string action: ""

        anchors.fill: parent
        screenOff: page.screenOff
        onAccepted: (index) => {
            const action = confirm.action
            confirm.close()
            if (index === confirm.acceptIndex)
                page.acted(action)
        }
    }

    AdjustOverlay {
        id: adjust

        anchors.fill: parent
        page: page
    }
}
