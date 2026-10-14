pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool shown: false
    property bool screenOff: false
    property string title: ""
    property var items: []
    property int focusIndex: 0
    property bool pressed: false
    readonly property bool animating: openAnim.running || closeAnim.running
    readonly property int duration: Motion.dialog
    readonly property int hideDuration: Motion.dialogHide
    readonly property real fromScale: Motion.dialogScale
    readonly property real scrimAlpha: Motion.dialogScrim
    readonly property color scrimColor: Ui.scrimOverlay
    readonly property real pitch: Ui.px(Ui.settingsPitch)
    readonly property real edge: Ui.px(Ui.edge)
    property alias scrim: scrim
    property alias body: body
    property alias titleText: titleText
    property alias bar: bar
    property alias openAnimation: openAnim
    property alias closeAnimation: closeAnim
    readonly property string touchKind: root.shown ? "" : "block"

    signal accepted(int index)
    signal rejected()

    visible: (root.shown || root.animating) && !root.screenOff

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
        opacity: 0
    }

    Item {
        id: body

        anchors.fill: parent
        transformOrigin: Item.Center
        opacity: 0
        scale: 1

        Text {
            id: titleText

            readonly property string touchKind: "block"

            x: root.edge
            y: Ui.px(Ui.pivotBaseline) - titleText.baselineOffset
            text: root.title
            color: Ui.white(Ui.dialogTitle.alpha)
            font: Ui.font(Ui.dialogTitle)
        }

        Column {
            id: list

            x: 0
            y: Ui.px(Ui.contentTop)
            width: body.width

            Repeater {
                model: root.items

                delegate: PressScale {
                    id: row

                    required property int index
                    required property var modelData
                    readonly property bool isFocused: row.index === root.focusIndex
                    readonly property string touchKind: root.shown ? "item" : ""
                    readonly property int touchIndex: row.index

                    pressed: row.isFocused && root.pressed
                    animated: !root.screenOff
                    width: list.width
                    height: root.pitch

                    Text {
                        x: root.edge
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData
                        color: Ui.white(row.isFocused ? Ui.dialogItem.alpha : Ui.dialogItem.alphaDim)
                        font: Ui.font(Ui.dialogItem)

                        Behavior on color {
                            enabled: !root.screenOff
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
            rowY: list.y + root.focusIndex * root.pitch
            rowHeight: root.pitch
            visible: root.items.length > 0
            animated: !root.screenOff
        }
    }

    ParallelAnimation {
        id: openAnim

        NumberAnimation {
            target: scrim
            property: "opacity"
            to: 1
            duration: Motion.dialog
            easing.type: Motion.pop
        }
        NumberAnimation {
            target: body
            property: "opacity"
            to: 1
            duration: Motion.dialog
            easing.type: Motion.pop
        }
        NumberAnimation {
            target: body
            property: "scale"
            from: Motion.dialogScale
            to: 1
            duration: Motion.dialog
            easing.type: Motion.bezierPop
        }
    }

    ParallelAnimation {
        id: closeAnim

        NumberAnimation {
            target: scrim
            property: "opacity"
            to: 0
            duration: Motion.dialogHide
            easing.type: Motion.pop
        }
        NumberAnimation {
            target: body
            property: "opacity"
            to: 0
            duration: Motion.dialogHide
            easing.type: Motion.pop
        }
    }

    function land() {
        scrim.opacity = root.shown ? 1 : 0
        body.opacity = root.shown ? 1 : 0
        body.scale = 1
    }

    function open() {
        openAnim.stop()
        closeAnim.stop()
        root.focusIndex = 0
        root.shown = true
        if (root.screenOff) {
            root.land()
            return
        }
        openAnim.start()
    }

    function close() {
        root.pressed = false
        openAnim.stop()
        closeAnim.stop()
        root.shown = false
        if (root.screenOff) {
            root.land()
            return
        }
        closeAnim.start()
    }

    function complete() {
        if (openAnim.running)
            openAnim.complete()
        if (closeAnim.running)
            closeAnim.complete()
        root.land()
    }

    function moveFocus(delta: int): bool {
        if (root.items.length === 0)
            return false
        root.focusIndex = Math.max(0, Math.min(root.items.length - 1, root.focusIndex + delta))
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
        if (!root.shown)
            return false
        switch (name) {
        case "up":
            return root.moveFocus(-1)
        case "down":
            return root.moveFocus(1)
        case "a":
            if (!repeat)
                root.accepted(root.focusIndex)
            return true
        case "b":
        case "start":
        case "back":
            if (!repeat) {
                root.rejected()
                root.close()
            }
            return true
        default:
            return true
        }
    }

    onScreenOffChanged: {
        if (root.screenOff)
            root.complete()
    }
}
