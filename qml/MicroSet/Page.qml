pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: page

    property PageStack stack: null
    property FocusList list: null
    property int focusIndex: 0
    property int focusCount: page.list !== null ? page.list.count : 0
    property bool pressed: false
    property int pageId: -1
    property bool arrived: false
    readonly property bool active: page.stack !== null && page.stack.currentItem === page
    readonly property real dialogFade: page.stack !== null ? page.stack.dialogFade : 0
    readonly property bool dialogUp: page.dialogFade > 0
    readonly property real focusOpacity: 1 - page.dialogFade
    readonly property bool stackAnimates: page.stack !== null && page.stack.animate === true
    property string touchKind: "page"

    signal action(string name, bool repeat)
    signal activated(int index)
    signal entered(bool first, string kind)
    signal leaving()

    function focusItem(): Item {
        return page.list !== null ? page.list.focusItem() : null
    }

    function handleAction(name: string, repeat: bool): bool {
        switch (name) {
        case "up":
            return page.moveFocus(-1)
        case "down":
            return page.moveFocus(1)
        case "lt":
            return page.pageFocus(-1)
        case "rt":
            return page.pageFocus(1)
        case "a":
            if (!repeat)
                page.activated(page.focusIndex)
            return true
        case "b":
            if (repeat || page.stack === null || page.stack.depth < 2)
                return false
            page.stack.pop()
            return true
        default:
            return false
        }
    }

    function moveFocus(delta: int): bool {
        if (page.list !== null) {
            const moved = page.list.moveFocus(delta)
            page.focusIndex = page.list.focusIndex
            return moved
        }
        if (page.focusCount <= 0)
            return false
        page.focusIndex = Math.max(0, Math.min(page.focusCount - 1, page.focusIndex + delta))
        return true
    }

    function pageFocus(direction: int): bool {
        if (page.list === null)
            return false
        const moved = page.list.pageFocus(direction)
        page.focusIndex = page.list.focusIndex
        return moved
    }

    function setFocus(index: int) {
        if (page.list !== null) {
            page.list.snapTo(index)
            page.focusIndex = page.list.focusIndex
            return
        }
        page.focusIndex = Math.max(0, Math.min(Math.max(0, page.focusCount - 1), index))
    }

    function dispatch(name: string, repeat: bool): bool {
        page.action(name, repeat)
        return page.handleAction(name, repeat)
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
        switch (kind) {
        case "row":
            if (page.list === null)
                return ""
            page.list.touchFocus(index)
            return "a"
        case "back":
        case "header":
            return "b"
        default:
            return ""
        }
    }

    function touchTap(kind: string, index: int, hit: Item) {
        if (kind === "row" && page.list !== null)
            page.list.ensureVisible(Motion.scroll)
    }

    Component.onCompleted: {
        if (page.stack !== null || page.parent === null)
            return
        const owner = page.parent.parent
        if (owner !== null && owner !== undefined && typeof owner.pop === "function" && typeof owner.completeAll === "function")
            page.stack = owner
    }

    Connections {
        target: page.list

        function onTouchFocused(index) {
            if (typeof page.syncFocus === "function")
                page.syncFocus()
            else
                page.focusIndex = index
        }
    }

    Connections {
        target: page.stack
        ignoreUnknownSignals: true

        function onEntered(item, kind) {
            if (item !== page)
                return
            const first = !page.arrived
            page.entered(first, kind)
            page.arrived = true
        }

        function onLeaving(item) {
            if (item === page)
                page.leaving()
        }
    }
}
