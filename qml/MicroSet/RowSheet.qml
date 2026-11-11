pragma ComponentBehavior: Bound
import QtQuick

Sheet {
    id: root

    property var rows: []
    property bool covered: false
    property bool pressed: false
    property int pressedIndex: -1
    readonly property var leadRows: root.rows.filter((r) => r.kind === "info")
    readonly property var entries: root.rows.filter((r) => r.kind !== "info")
    property alias cells: cells
    property alias leadLine: leadLine

    items: root.entries.map(() => "")
    keepLead: root.leadRows.length > 0
    opacity: root.covered ? 0 : 1

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (!root.shown)
            return ""
        switch (kind) {
        case "item":
            root.pressedIndex = index
            root.focusIndex = index
            return "a"
        case "scrim":
            return "b"
        default:
            return ""
        }
    }

    function press(name: string) {
        if (name === "a")
            root.pressed = true
    }

    function release(name: string) {
        if (name === "a")
            root.pressed = false
    }

    onPressedChanged: {
        if (!root.pressed)
            root.pressedIndex = -1
    }

    Text {
        id: leadLine

        parent: root.panel
        x: root.edge
        y: root.edge + root.titleHeight
        width: Math.max(0, root.panel.width - 2 * root.edge)
        height: root.leadHeight
        visible: root.leadRows.length > 0
        verticalAlignment: Text.AlignVCenter
        clip: true
        text: root.leadRows.length > 0 ? root.leadRows[0].title : ""
        textFormat: Text.PlainText
        color: Ui.white(Ui.dialogItem.alphaDim)
        font: Ui.font(Ui.dialogItem)
    }

    Repeater {
        id: cells

        parent: root.window.contentItem
        model: root.entries

        delegate: Item {
            id: cell

            required property int index
            required property var modelData
            readonly property bool focused: cell.index === root.focusIndex
            readonly property bool pressTarget: cell.index === root.pressedIndex && cell.modelData.installed !== undefined
            readonly property Item titleItem: title
            readonly property Item valueItem: value

            x: 0
            y: cell.index * root.pitch
            width: root.window.width
            height: root.pitch

            Text {
                id: value

                x: cell.width - root.edge - value.width
                anchors.verticalCenter: parent.verticalCenter
                visible: value.text !== ""
                text: cell.modelData.value
                textFormat: Text.PlainText
                color: Ui.white(cell.focused ? Ui.settingsValueFocused : Ui.settingsValueDim)
                font: Ui.font(Ui.settingsValue)

                Behavior on color {
                    enabled: root.animated
                    ColorAnimation {
                        duration: Motion.focusAlpha
                    }
                }
            }

            Text {
                id: title

                x: root.edge
                width: Math.max(0, (value.visible ? value.x - Ui.px(Ui.gutter) : cell.width - root.edge) - title.x)
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                text: cell.modelData.title
                textFormat: Text.PlainText
                color: Ui.white(root.pressed && cell.pressTarget ? Ui.alphaFull
                    : cell.focused ? Ui.dialogItem.alpha : Ui.dialogItem.alphaDim)
                font: root.plainFont

                Behavior on color {
                    enabled: root.animated && !cell.pressTarget
                    ColorAnimation {
                        duration: Motion.focusAlpha
                    }
                }
            }
        }
    }
}
