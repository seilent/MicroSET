pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: layer

    property PadRouter router: null
    property var dialogs: []
    property bool dispatching: false
    property bool down: false
    property bool dead: false
    property bool tapping: false
    property bool dragging: false
    property bool tracking: false
    property int pointId: -1
    property Item owner: null
    property Item page: null
    property Item hit: null
    property int hitIndex: -1
    property string kind: ""
    property string action: ""
    property Item modal: null
    property int depth: 0
    property int rowCount: -1
    property var shownDialogs: []
    property Item scroller: null
    property Item list: null
    property Item frame: null
    property Item pressedPage: null
    property Item returnPage: null
    property point start: Qt.point(0, 0)
    property point last: Qt.point(0, 0)
    property var samples: []
    property var pending: null
    readonly property TouchScroll scroll: layer.scroller !== null && layer.scroller.touch !== undefined ? layer.scroller.touch : null
    readonly property TouchScroll moving: layer.flung !== null ? layer.flung : layer.scroll
    property TouchScroll flung: null
    property alias handler: handler
    property alias returnTimer: returnTimer
    property alias holdTimer: holdTimer

    signal touched()

    function participates(item: Item): bool {
        return item.visible && item.enabled && item.opacity > 0
    }

    function tagOf(item: Item): string {
        return typeof item.touchKind === "string" ? item.touchKind : ""
    }

    function ordered(item: Item): var {
        const kids = []
        for (let i = 0; i < item.children.length; i++)
            kids.push({ item: item.children[i], order: i })
        kids.sort((a, b) => b.item.z - a.item.z || b.order - a.order)
        return kids.map((k) => k.item)
    }

    function visit(item: Item, p: point): var {
        if (item === null || !layer.participates(item))
            return null
        const local = item.mapFromItem(null, p.x, p.y)
        const inside = item.contains(local)
        if (item.clip && !inside)
            return null
        let found = null
        let loose = null
        const rows = typeof item.indexAt === "function" && typeof item.itemAtIndex === "function"
        const kids = rows ? [] : layer.ordered(item)
        if (rows && inside) {
            const index = item.indexAt(local.x + item.contentX, local.y + item.contentY)
            const row = index >= 0 ? item.itemAtIndex(index) : null
            if (row !== null)
                kids.push(row)
        }
        for (const kid of kids) {
            const r = layer.visit(kid, p)
            if (r === null)
                continue
            if (r.hit !== null) {
                found = r
                break
            }
            if (loose === null)
                loose = r.scroller
        }
        const own = inside && item.touchScroll === true && item.touchScrollEnabled !== false ? item : null
        if (found !== null) {
            if (found.scroller === null && !found.block)
                found.scroller = own
            return found
        }
        const tag = layer.tagOf(item)
        if (inside && tag !== "") {
            const block = tag === "block"
            return { hit: item, block: block, scroller: block ? null : loose !== null ? loose : own }
        }
        const scroller = loose !== null ? loose : own
        return scroller === null ? null : { hit: null, block: false, scroller: scroller }
    }

    function walk(root: Item, p: point): var {
        const r = layer.visit(root, p)
        return r === null ? { hit: null, block: false, scroller: null } : r
    }

    function ancestorWith(item: Item, name: string): Item {
        for (let it = item; it !== null; it = it.parent) {
            if (typeof it[name] === "function")
                return it
        }
        return null
    }

    function listOf(item: Item): Item {
        for (let it = item; it !== null; it = it.parent) {
            if (it.touchScroll === true && typeof it.indexAt === "function")
                return it
        }
        return null
    }

    function targetRoot(): Item {
        for (const d of layer.dialogs) {
            if (d !== null && d !== undefined && (d.shown === true || d.animating === true))
                return d
        }
        return layer.router !== null && layer.router.stack !== null ? layer.router.stack.currentItem : null
    }

    function shownNow(): var {
        return layer.dialogs.filter((d) => d !== null && d !== undefined && d.shown === true)
    }

    function rowsOf(item: Item): int {
        return item !== null && item.rows !== undefined && item.rows !== null && item.rows.length !== undefined ? item.rows.length : -1
    }

    function runReturn() {
        returnTimer.stop()
        const p = layer.returnPage
        layer.returnPage = null
        if (p !== null && typeof p.release === "function")
            p.release("a")
    }

    function releasePressed() {
        const p = layer.pressedPage
        layer.pressedPage = null
        if (p !== null && typeof p.release === "function")
            p.release("a")
    }

    function stopFling() {
        if (layer.flung !== null)
            layer.flung.stop()
        layer.flung = null
    }

    function clear() {
        holdTimer.stop()
        layer.down = false
        layer.tapping = false
        layer.dragging = false
        layer.tracking = false
        layer.pending = null
        layer.owner = null
        layer.page = null
        layer.hit = null
        layer.hitIndex = -1
        layer.kind = ""
        layer.action = ""
        layer.modal = null
        layer.scroller = null
        layer.list = null
        layer.frame = null
        layer.samples = []
    }

    function endTrack() {
        if (!layer.tracking)
            return
        layer.tracking = false
        layer.owner.touchCancel(layer.kind, layer.hitIndex, layer.hit)
    }

    function abort() {
        holdTimer.stop()
        layer.endTrack()
        layer.stopFling()
        if (layer.dragging && layer.scroll !== null)
            layer.scroll.stop()
        layer.releasePressed()
        if (!layer.down)
            return
        if (layer.pending !== null) {
            layer.clear()
            return
        }
        layer.dead = true
        layer.tapping = false
        layer.dragging = false
        layer.pending = null
    }

    function cancel() {
        if (layer.dispatching)
            return
        layer.abort()
    }

    function scenePoint(): point {
        return handler.point.scenePosition
    }

    function localIn(item: Item, p: point): point {
        return item.mapFromItem(null, p.x, p.y)
    }

    function sample(y: real) {
        const now = Date.now()
        const kept = []
        let old = null
        for (const s of layer.samples) {
            if (s.t < now - Motion.flingWindow)
                old = s
            else
                kept.push(s)
        }
        if (old !== null)
            kept.unshift(old)
        kept.push({ t: now, y: y })
        layer.samples = kept
    }

    function velocity(samples: var): real {
        if (samples.length < 2)
            return 0
        const r = samples[samples.length - 1]
        const at = r.t - Motion.flingWindow
        let anchor = samples[0]
        if (anchor.t < at) {
            for (let i = 0; i < samples.length - 1; i++) {
                const a = samples[i]
                const b = samples[i + 1]
                if (a.t <= at && b.t >= at) {
                    const f = b.t > a.t ? (at - a.t) / (b.t - a.t) : 0
                    anchor = { t: at, y: a.y + (b.y - a.y) * f }
                    break
                }
            }
        }
        const dt = r.t - anchor.t
        if (dt <= 0)
            return 0
        return (r.y - anchor.y) / dt * 1000
    }

    function pressAt(p: point) {
        layer.runReturn()
        layer.stopFling()
        layer.clear()
        layer.down = true
        layer.dead = true
        layer.pointId = handler.point.id
        const router = layer.router
        if (router === null || router.screenOff || router.locked)
            return
        if (router.saverUp) {
            layer.dispatching = true
            try {
                router.dispatch("a", false)
            } finally {
                layer.dispatching = false
            }
            return
        }
        const root = layer.targetRoot()
        if (root === null)
            return
        let found = layer.walk(root, p)
        if (found.block)
            return
        if (typeof root.completeAll === "function")
            root.completeAll()
        else if (typeof root.complete === "function")
            root.complete()
        found = layer.walk(root, p)
        if (found.block)
            return
        layer.dead = false
        layer.hit = found.hit
        layer.scroller = found.scroller
        layer.list = layer.listOf(found.scroller !== null ? found.scroller : found.hit)
        layer.kind = found.hit !== null ? layer.tagOf(found.hit) : ""
        layer.hitIndex = found.hit !== null && found.hit.touchIndex !== undefined ? found.hit.touchIndex : -1
        layer.owner = layer.ancestorWith(found.hit, "touchPress")
        layer.page = router.stack !== null ? router.stack.currentItem : null
        layer.modal = router.modal
        layer.depth = router.stack !== null ? router.stack.depth : 0
        layer.rowCount = layer.rowsOf(layer.page)
        layer.shownDialogs = layer.shownNow()
        layer.frame = layer.scroller !== null && layer.scroll !== null ? layer.scroll.flick : found.hit
        if (layer.owner !== null) {
            const x = layer.localIn(found.hit, p).x
            layer.action = String(layer.owner.touchPress(layer.kind, layer.hitIndex, x, found.hit))
            if (layer.action === "" && found.hit.touchTrack === true && typeof layer.owner.touchMove === "function") {
                layer.tracking = true
                layer.frame = found.hit
            }
        }
        if (layer.action === "a") {
            const presser = layer.ancestorWith(found.hit, "press")
            if (presser !== null) {
                presser.press("a")
                layer.pressedPage = presser
            }
        }
        if (layer.frame === null) {
            layer.dead = true
            layer.releasePressed()
        } else {
            layer.start = layer.localIn(layer.frame, p)
            layer.last = layer.start
            layer.tapping = true
            layer.sample(layer.start.y)
            if (!layer.tracking && layer.owner !== null && typeof layer.owner.touchContext === "function")
                holdTimer.restart()
        }
        layer.touched()
    }

    function hold() {
        if (!layer.down || layer.dead || !layer.tapping || layer.pending !== null || !layer.intact())
            return
        let handled = false
        layer.dispatching = true
        try {
            handled = layer.router.context(layer.owner, layer.kind, layer.hitIndex, layer.hit)
        } finally {
            layer.dispatching = false
        }
        if (!handled)
            return
        layer.releasePressed()
        layer.tapping = false
        layer.dead = true
        layer.touched()
    }

    function moveAt(p: point) {
        if (!layer.down || layer.dead || layer.frame === null)
            return
        const local = layer.localIn(layer.frame, p)
        layer.last = local
        if (layer.tracking) {
            layer.owner.touchMove(layer.kind, layer.hitIndex, local.x - layer.start.x, local.y - layer.start.y, layer.hit)
            return
        }
        layer.sample(local.y)
        if (layer.dragging) {
            layer.scroll.dragTo(local.y)
            return
        }
        if (!layer.tapping)
            return
        const dx = local.x - layer.start.x
        const dy = local.y - layer.start.y
        const slop = Ui.px(Ui.touchSlop)
        if (dx * dx + dy * dy <= slop * slop)
            return
        layer.tapping = false
        holdTimer.stop()
        layer.releasePressed()
        if (layer.scroll === null || Math.abs(dy) < Math.abs(dx)) {
            layer.dead = true
            return
        }
        layer.dragging = true
        layer.scroll.begin(local.y)
        layer.touched()
    }

    function lift(fresh: bool) {
        if (!layer.down)
            return
        if (layer.dead) {
            layer.clear()
            return
        }
        const local = fresh && layer.frame !== null ? layer.localIn(layer.frame, layer.scenePoint()) : layer.last
        layer.pending = { local: local }
        Qt.callLater(layer.commit)
    }

    function intact(): bool {
        const router = layer.router
        if (router === null || router.saverUp || router.locked || router.screenOff || router.modal !== layer.modal)
            return false
        if (router.stack === null || router.stack.currentItem !== layer.page || router.stack.depth !== layer.depth)
            return false
        for (const d of layer.shownNow()) {
            if (layer.shownDialogs.indexOf(d) < 0)
                return false
        }
        if (layer.hit === null)
            return layer.kind === ""
        const index = layer.hit.touchIndex !== undefined ? layer.hit.touchIndex : -1
        return index === layer.hitIndex
    }

    function commit() {
        const pending = layer.pending
        if (pending === null || !layer.down)
            return
        layer.pending = null
        if (layer.dragging) {
            layer.sample(pending.local.y)
            const scroll = layer.scroll
            const v = layer.velocity(layer.samples)
            layer.clear()
            if (Math.abs(v) >= Ui.px(Ui.flingMin)) {
                layer.flung = scroll
                scroll.release(-Math.max(-Ui.px(Ui.flingCap), Math.min(Ui.px(Ui.flingCap), v)))
            }
            layer.touched()
            return
        }
        if (layer.tracking) {
            const owner = layer.owner
            const kind = layer.kind
            const index = layer.hitIndex
            const hit = layer.hit
            const dx = pending.local.x - layer.start.x
            const dy = pending.local.y - layer.start.y
            const whole = layer.intact()
            layer.tracking = false
            layer.clear()
            if (whole)
                owner.touchLift(kind, index, dx, dy, hit)
            else
                owner.touchCancel(kind, index, hit)
            layer.touched()
            return
        }
        if (!layer.tapping) {
            layer.clear()
            return
        }
        if (!layer.intact()) {
            layer.releasePressed()
            layer.clear()
            return
        }
        const owner = layer.owner
        const kind = layer.kind
        const index = layer.hitIndex
        const hit = layer.hit
        const action = layer.action
        const pressed = layer.pressedPage
        layer.pressedPage = null
        layer.clear()
        if (owner !== null && typeof owner.touchTap === "function")
            owner.touchTap(kind, index, hit)
        if (["a", "b", "left", "right"].indexOf(action) >= 0) {
            layer.dispatching = true
            try {
                layer.router.dispatch(action, false)
            } finally {
                layer.dispatching = false
            }
        }
        if (pressed !== null) {
            layer.returnPage = pressed
            returnTimer.restart()
        }
        layer.touched()
    }

    onEnabledChanged: {
        if (!layer.enabled)
            layer.cancel()
    }

    PointHandler {
        id: handler

        enabled: layer.enabled

        onActiveChanged: {
            if (handler.active)
                layer.pressAt(layer.scenePoint())
            else
                layer.lift(handler.point.id === layer.pointId)
        }
        onPointChanged: {
            if (handler.active)
                layer.moveAt(layer.scenePoint())
        }
        onGrabChanged: (transition, point) => {
            if (transition === PointerDevice.CancelGrabPassive || transition === PointerDevice.CancelGrabExclusive)
                layer.cancel()
        }
        onCanceled: layer.cancel()
    }

    Timer {
        id: holdTimer

        interval: Ui.seekHoldMs
        repeat: false
        onTriggered: layer.hold()
    }

    Timer {
        id: returnTimer

        interval: Motion.touchReturnDelay
        repeat: false
        onTriggered: layer.runReturn()
    }

    Connections {
        target: layer.router

        function onRouted() {
            if (layer.dispatching)
                return
            layer.cancel()
        }

        function onModalUpChanged() {
            if (layer.router.modalUp)
                layer.cancel()
        }

        function onSaverUpChanged() {
            if (layer.router.saverUp)
                layer.cancel()
        }

        function onLockedChanged() {
            if (layer.router.locked)
                layer.cancel()
        }

        function onScreenOffChanged() {
            if (layer.router.screenOff)
                layer.cancel()
        }
    }

    Connections {
        target: layer.router !== null ? layer.router.stack : null

        function onCurrentItemChanged() {
            layer.cancel()
        }
    }

    Connections {
        target: layer.list
        ignoreUnknownSignals: true

        function onCountChanged() {
            layer.cancel()
        }

        function onModelChanged() {
            layer.cancel()
        }
    }

    Connections {
        target: layer.page
        ignoreUnknownSignals: true

        function onRowsChanged() {
            if (layer.rowsOf(layer.page) !== layer.rowCount)
                layer.cancel()
        }
    }

    Connections {
        target: layer.moving !== null ? layer.moving.flick : null

        function onContentYChanged() {
            const scroll = layer.moving
            if ((layer.dragging || scroll.flinging) && scroll.flick.contentY !== scroll.lastWrite)
                layer.cancel()
        }
    }
}
