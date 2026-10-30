pragma ComponentBehavior: Bound
import QtQuick

PressScale {
    id: root

    property string primary: ""
    property string secondary: ""
    property string number: ""
    property string trailing: ""
    property bool focused: false
    property bool playing: false
    property bool bold: false
    property bool countLine: false
    property bool ring: false
    property string ringGlyph: "play"
    property real ringArc: 0
    property bool ringToggle: false
    property bool ringOn: false

    readonly property bool indented: root.number !== "" || root.ring
    readonly property real textX: Ui.px(Ui.hubEdge) + (root.indented ? Ui.songTextX(root.number.length) : 0)
    readonly property var primaryRole: root.bold ? Ui.listPlaylist : Ui.listPrimary
    readonly property var secondaryRole: root.bold || root.countLine ? Ui.listCount : Ui.listSecondary
    readonly property real primaryAlpha: root.focused ? root.primaryRole.alpha : root.primaryRole.alphaDim
    readonly property color primaryColor: root.playing ? Ui.accent : Ui.white(root.primaryAlpha)
    readonly property real primaryBaseline: primaryText.y + primaryText.baselineOffset
    readonly property real secondaryBaseline: secondaryText.y + secondaryText.baselineOffset
    readonly property Item primaryItem: primaryText
    readonly property Item numberItem: numberText
    readonly property Item secondaryItem: secondaryText
    readonly property Item trailingItem: trailingText
    readonly property Item ringItem: ringLoader.item

    width: Ui.windowW
    height: Ui.px(Ui.rowHeight)

    Loader {
        id: ringLoader

        active: root.ring
        x: Ui.px(Ui.hubEdge)
        anchors.verticalCenter: parent.verticalCenter

        sourceComponent: Ring {
            size: Ui.rowRing
            glyph: root.ringGlyph
            focused: root.focused
            arcSweep: root.ringArc
            toggle: root.ringToggle
            on: root.ringOn
            animated: root.animated
        }
    }

    Text {
        id: numberText

        visible: root.number !== ""
        x: Ui.px(Ui.hubEdge)
        anchors.baseline: primaryText.baseline
        text: root.number
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
    }

    Text {
        id: primaryText

        x: root.textX
        y: root.secondary !== "" ? Ui.px(Ui.rowTextInset) : (root.height - primaryText.height) / 2
        width: trailingText.visible ? trailingText.x - Ui.px(Ui.gutter) - primaryText.x : root.width - Ui.px(Ui.hubEdge) - primaryText.x
        text: root.primary
        textFormat: Text.PlainText
        color: root.primaryColor
        font: Ui.font(root.primaryRole)
        clip: true

        Behavior on color {
            enabled: root.animated
            ColorAnimation {
                duration: Motion.focusAlpha
            }
        }
    }

    Text {
        id: secondaryText

        visible: root.secondary !== ""
        x: root.textX
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Ui.px(Ui.rowTextInset)
        width: root.width - Ui.px(Ui.hubEdge) - secondaryText.x
        text: root.secondary
        textFormat: Text.PlainText
        color: Ui.white(root.secondaryRole.alpha)
        font: Ui.font(root.secondaryRole)
        clip: true
    }

    Text {
        id: trailingText

        visible: root.trailing !== ""
        x: root.width - Ui.px(Ui.hubEdge) - trailingText.width
        anchors.baseline: primaryText.baseline
        text: root.trailing
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
    }
}
