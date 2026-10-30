pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

PressScale {
    id: root

    property real size: Ui.npRing
    property bool focused: false
    property bool on: false
    property bool toggle: false
    property string glyph: ""
    property real glyphSize: root.size / 2
    property real level: 1.0
    property real arcSweep: 0
    readonly property alias iconItem: icon
    readonly property bool swapping: swap.running

    readonly property real strokeWidth: Ui.px(root.focused ? Ui.ringStrokeFocused : Ui.ringStrokeIdle)
    readonly property bool lit: root.toggle ? root.on : root.focused
    readonly property real ringAlpha: root.lit ? Ui.alphaFull : Ui.alphaMid
    readonly property color ringColor: Ui.white(root.ringAlpha)

    property bool completed: false
    property string shownGlyph: ""

    function toggled() {
        if (root.animated)
            swap.restart()
    }

    width: Ui.px(root.size)
    height: Ui.px(root.size)

    onGlyphChanged: {
        if (root.completed && root.animated && root.shownGlyph !== "" && root.glyph !== "")
            swap.restart()
        root.shownGlyph = root.glyph
    }

    onAnimatedChanged: {
        if (!root.animated && swap.running)
            swap.complete()
    }

    Component.onCompleted: {
        root.shownGlyph = root.glyph
        root.completed = true
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: root.strokeWidth
        border.color: root.ringColor

        Behavior on border.color {
            enabled: root.animated
            ColorAnimation {
                duration: Motion.focusAlpha
            }
        }
    }

    Shape {
        anchors.fill: parent
        visible: root.arcSweep > 0

        ShapePath {
            strokeColor: Ui.accent
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.FlatCap

            PathAngleArc {
                centerX: root.width / 2
                centerY: root.height / 2
                radiusX: (root.width - root.strokeWidth) / 2
                radiusY: (root.height - root.strokeWidth) / 2
                startAngle: Ui.ringArcStart
                sweepAngle: root.arcSweep
            }
        }
    }

    Glyph {
        id: icon

        anchors.centerIn: parent
        name: root.glyph
        size: root.glyphSize
        level: root.level
        color: root.ringColor
        transformOrigin: Item.Center
    }

    ParallelAnimation {
        id: swap

        NumberAnimation {
            target: icon
            property: "scale"
            from: Motion.toggleScale
            to: 1
            duration: Motion.toggle
            easing.type: Motion.bezierPop
        }

        NumberAnimation {
            target: icon
            property: "opacity"
            from: 0
            to: 1
            duration: Motion.toggleSwap
        }
    }
}
