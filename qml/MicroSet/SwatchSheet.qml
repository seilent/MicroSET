pragma ComponentBehavior: Bound
import QtQuick

Sheet {
    id: root

    property var colors: []
    readonly property real side: Ui.fontPx(Ui.dialogItem.size)
    readonly property real ring: Ui.px(Ui.focusBorder)
    readonly property real gap: Ui.px(Ui.focusGap)
    property alias cells: cells

    items: root.colors.map(() => "")

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        return root.shown && kind === "scrim" ? "b" : ""
    }

    function touchTap(kind: string, index: int, hit: Item) {
        if (!root.shown || kind !== "item")
            return
        root.focusIndex = index
        root.accepted(index)
    }

    Repeater {
        id: cells

        parent: root.window.contentItem
        model: root.colors

        delegate: Item {
            id: cell

            required property int index
            required property var modelData
            readonly property bool focused: cell.index === root.focusIndex

            x: root.edge
            y: cell.index * root.pitch + (root.pitch - root.side) / 2
            width: root.side
            height: root.side

            Rectangle {
                anchors.fill: parent
                color: cell.modelData
            }

            Rectangle {
                x: -(root.gap + root.ring)
                y: -(root.gap + root.ring)
                width: cell.width + 2 * (root.gap + root.ring)
                height: cell.height + 2 * (root.gap + root.ring)
                visible: cell.focused
                color: "transparent"
                border.width: root.ring
                border.color: Ui.white(Ui.alphaFull)
            }
        }
    }
}
