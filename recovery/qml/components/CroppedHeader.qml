pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string text: ""
    property real shift: 0
    property bool animated: true

    readonly property real xHeightBottom: Ui.headerXHeightBottom
    readonly property real baselineY: Ui.px(root.xHeightBottom)
    readonly property real textX: label.x
    readonly property real textBaseline: label.y + label.baselineOffset

    width: Ui.windowW
    height: Ui.px(Ui.headerBand)
    clip: true

    Text {
        id: label

        readonly property string touchKind: "header"

        x: Ui.px(Ui.hubEdge + root.shift)
        y: root.baselineY - label.baselineOffset
        text: root.text
        color: Ui.white(Ui.hubHeader.alpha)
        font: Ui.font(Ui.hubHeader)

        Behavior on x {
            enabled: root.animated
            NumberAnimation {
                duration: Motion.pivotStrip
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.strip
            }
        }
    }
}
