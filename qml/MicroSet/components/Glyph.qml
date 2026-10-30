pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import "GlyphPaths.js" as GlyphPaths

Item {
    id: root

    property string name: "play"
    property real size: Ui.glyphBox
    property real level: 1.0
    property color color: Ui.textFull

    readonly property real k: Ui.px(root.size) / GlyphPaths.box
    readonly property real strokeWidth: GlyphPaths.stroke * root.k
    readonly property bool filled: GlyphPaths.filled(root.name)
    readonly property bool known: GlyphPaths.has(root.name)

    width: Ui.px(root.size)
    height: Ui.px(root.size)

    Shape {
        anchors.fill: parent
        visible: root.known

        ShapePath {
            strokeColor: root.filled ? "transparent" : root.color
            strokeWidth: root.strokeWidth
            fillColor: root.filled ? root.color : "transparent"
            fillRule: ShapePath.WindingFill
            capStyle: ShapePath.FlatCap
            joinStyle: ShapePath.MiterJoin

            PathSvg {
                path: GlyphPaths.path(root.name, root.k)
            }
        }
    }

    Rectangle {
        visible: root.name === "battery"
        x: GlyphPaths.batteryLevel.x * root.k
        y: GlyphPaths.batteryLevel.y * root.k
        width: GlyphPaths.batteryLevel.w * root.k * Math.max(0, Math.min(1, root.level))
        height: GlyphPaths.batteryLevel.h * root.k
        color: root.color
    }
}
