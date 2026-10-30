pragma ComponentBehavior: Bound
import QtQuick

PressScale {
    id: root

    property string letter: "a"
    property real size: Ui.tileS
    property bool focused: false
    property bool empty: false

    readonly property real borderWidth: Ui.px(root.focused ? Ui.focusBorder : Ui.letterTileBorder)
    readonly property real borderAlpha: root.focused ? Ui.alphaFull : Ui.alphaLow
    readonly property real letterAlpha: root.empty ? Ui.alphaLow : Ui.letterGlyph.alpha
    readonly property real letterRightInset: Ui.letterInsetX
    readonly property real letterBaselineInset: Ui.letterInsetBaseline
    readonly property Item letterItem: glyph

    width: Ui.px(root.size)
    height: Ui.px(root.size)

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
        border.width: root.borderWidth
        border.color: Ui.white(root.borderAlpha)

        Behavior on border.color {
            enabled: root.animated
            ColorAnimation {
                duration: Motion.focusAlpha
            }
        }
    }

    Text {
        id: glyph

        anchors.right: parent.right
        anchors.rightMargin: Ui.px(root.letterRightInset)
        anchors.baseline: parent.bottom
        anchors.baselineOffset: -Ui.px(root.letterBaselineInset)
        text: root.letter
        color: Ui.white(root.letterAlpha)
        font: Ui.font(Ui.letterGlyph)
    }
}
