pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool animate: true
    property real dialogFade: 0
    property var pages: []
    readonly property int depth: root.pages.length
    readonly property Item currentItem: root.pages.length > 0 ? root.pages[root.pages.length - 1].content : null
    readonly property Item incoming: d.incoming
    readonly property Item outgoing: d.outgoing
    readonly property string phase: d.phase
    readonly property string transitionKind: d.kind
    readonly property bool inputLocked: d.phase !== "idle"

    signal entered(Item page, string kind)
    signal leaving(Item page)

    component Holder: Turnstile {
        id: holder

        property Item content: null
        property bool entering: true
        property bool revealed: true
        property bool flying: false
        property string kind: "turnstile"
        property string pushKind: "turnstile"
        property real fromAngle: 0
        property real toAngle: 0
        property int duration: Motion.turnstileIn
        property real progress: 1
        readonly property real elapsed: holder.progress * holder.duration
        readonly property bool fade: holder.kind === "fade"
        readonly property bool still: holder.kind === "home"
        readonly property bool splitFade: holder.content !== null && holder.content.splitFade === true
        readonly property Item header: holder.content !== null && holder.content.header !== undefined ? holder.content.header : null
        readonly property Item strip: holder.content !== null && holder.content.strip !== undefined ? holder.content.strip : null
        readonly property real fly: holder.flying ? Motion.expOut6(Math.min(1, holder.elapsed / Motion.headerOut)) : 0
        readonly property real flyY: holder.fly * Motion.headerOutShift * holder.height
        readonly property real backdropFade: !holder.fade ? 1
            : holder.entering ? Motion.outExpo(holder.progress) : 1 - Motion.outExpo(holder.progress)
        readonly property real contentFade: !holder.fade ? 1
            : holder.entering ? Motion.inQuad(holder.progress)
            : 1 - Motion.inQuad(Math.min(1, holder.elapsed / Motion.nowPlayingOut))

        anchors.fill: holder.parent
        angle: holder.fromAngle + (holder.toAngle - holder.fromAngle)
            * (holder.entering ? Motion.expOut6(holder.progress) : Motion.expIn6(holder.progress))
        opacity: holder.still ? (holder.revealed ? 1 : 0)
            : holder.fade ? (holder.splitFade ? 1 : holder.backdropFade)
            : holder.entering ? Math.min(1, holder.elapsed / Motion.turnstileInFade)
            : (holder.elapsed < Motion.turnstileOutHold ? 1 : 0)

        Binding {
            target: holder.header
            property: "y"
            value: holder.flyY
            when: holder.flying && holder.header !== null
        }

        Binding {
            target: holder.header
            property: "opacity"
            value: 1 - holder.fly
            when: holder.flying && holder.header !== null
        }

        Binding {
            target: holder.strip
            property: "y"
            value: holder.flyY
            when: holder.flying && holder.strip !== null
        }

        Binding {
            target: holder.strip
            property: "opacity"
            value: 1 - holder.fly
            when: holder.flying && holder.strip !== null
        }
    }

    Component {
        id: holderComponent
        Holder {}
    }

    QtObject {
        id: d

        property Holder outgoing: null
        property Holder incoming: null
        property bool removeOutgoing: false
        property string phase: "idle"
        property string kind: "turnstile"

        function motionOf(holder): string {
            if (holder.content !== null && holder.content.pageMotion !== undefined)
                return String(holder.content.pageMotion)
            return ""
        }

        function exitDuration(holder): int {
            if (holder.content !== null && holder.content.exitDuration !== undefined)
                return Number(holder.content.exitDuration)
            return Motion.turnstileOut
        }

        function begin(from, to, remove, kind, pop) {
            d.outgoing = from
            d.incoming = to
            d.removeOutgoing = remove
            d.kind = kind
            const fade = kind === "fade"
            const fromHome = !pop && !fade && d.motionOf(from) === "home"
            const toHome = pop && !fade && d.motionOf(to) === "home"
            from.kind = fromHome ? "home" : kind
            from.entering = false
            from.revealed = true
            from.flying = pop && !fade
            from.fromAngle = 0
            from.toAngle = fade || fromHome ? 0 : (pop ? Motion.turnstileInAngle : Motion.turnstileOutAngle)
            from.duration = fade ? Motion.nowPlayingFade : fromHome ? d.exitDuration(from) : Motion.turnstileOut
            from.progress = 0
            from.visible = true
            to.kind = toHome ? "home" : kind
            to.entering = true
            to.revealed = false
            to.flying = false
            to.fromAngle = fade || toHome ? 0 : (pop ? Motion.turnstileOutAngle : Motion.turnstileInAngle)
            to.toAngle = 0
            to.duration = fade ? Motion.nowPlayingIn : toHome ? 0 : Motion.turnstileIn
            to.progress = 0
            to.visible = true
            d.phase = "exit"
            if (fromHome)
                root.leaving(from.content)
            transition.start()
        }

        function showIncoming() {
            if (d.outgoing)
                d.outgoing.visible = false
            if (d.incoming)
                d.incoming.revealed = true
            d.phase = "enter"
        }

        function finish() {
            const page = d.incoming !== null ? d.incoming.content : null
            const kind = d.incoming !== null ? d.incoming.kind : ""
            if (d.outgoing) {
                d.outgoing.visible = false
                if (d.removeOutgoing)
                    d.outgoing.destroy()
            }
            if (d.incoming)
                d.incoming.revealed = true
            d.outgoing = null
            d.incoming = null
            d.removeOutgoing = false
            d.phase = "idle"
            if (page !== null)
                root.entered(page, kind)
        }
    }

    SequentialAnimation {
        id: transition

        NumberAnimation {
            target: d.outgoing
            property: "progress"
            from: 0
            to: 1
            duration: d.outgoing !== null ? d.outgoing.duration : Motion.turnstileOut
            easing.type: Motion.linear
        }
        ScriptAction {
            script: d.showIncoming()
        }
        NumberAnimation {
            target: d.incoming
            property: "progress"
            from: 0
            to: 1
            duration: d.incoming !== null ? d.incoming.duration : Motion.turnstileIn
            easing.type: Motion.linear
        }
        onFinished: d.finish()
    }

    function push(component, properties, kind) {
        if (d.phase !== "idle")
            root.completeAll()
        const transitionKind = kind === undefined ? "turnstile" : kind
        const holder = holderComponent.createObject(root)
        const props = properties === undefined ? {} : properties
        if (props.stack === undefined)
            props.stack = root
        const content = component.createObject(holder, props)
        content.anchors.fill = holder
        holder.content = content
        holder.pushKind = transitionKind
        holder.kind = transitionKind
        const previous = root.pages.length > 0 ? root.pages[root.pages.length - 1] : null
        root.pages = root.pages.concat([holder])
        if (previous === null || !root.animate) {
            holder.progress = 1
            holder.visible = true
            if (previous !== null)
                previous.visible = false
            root.entered(content, "")
            return content
        }
        d.begin(previous, holder, false, transitionKind, false)
        return content
    }

    function pop() {
        if (root.pages.length < 2)
            return
        if (d.phase !== "idle")
            root.completeAll()
        const top = root.pages[root.pages.length - 1]
        const below = root.pages[root.pages.length - 2]
        root.pages = root.pages.slice(0, root.pages.length - 1)
        if (!root.animate) {
            below.progress = 1
            below.revealed = true
            below.visible = true
            top.destroy()
            root.entered(below.content, "")
            return
        }
        d.begin(top, below, true, top.pushKind, true)
    }

    function completeAll() {
        if (d.phase === "idle")
            return
        transition.stop()
        if (d.outgoing)
            d.outgoing.progress = 1
        if (d.incoming) {
            d.incoming.progress = 1
            d.incoming.visible = true
        }
        d.finish()
    }

    function eachPage(fn: var): void {
        for (const holder of root.pages) {
            if (holder.content !== null && holder.content !== undefined)
                fn(holder.content)
        }
    }

    function serialize(): var {
        const out = []
        for (const holder of root.pages) {
            const content = holder.content
            if (content === null || content === undefined)
                continue
            const name = String(content.objectName)
            if (name === "")
                continue
            const id = content.pageId === undefined ? -1 : Number(content.pageId)
            out.push({ name: name, id: id })
        }
        return out
    }

    function clear() {
        root.completeAll()
        const holders = root.pages
        root.pages = []
        for (const holder of holders)
            holder.destroy()
    }
}
