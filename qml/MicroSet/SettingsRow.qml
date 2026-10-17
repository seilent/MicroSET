pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: item

    required property int index
    required property var modelData
    required property Item page
    readonly property bool isFocused: item.page.focusIndex === item.index
    readonly property bool editing: item.page.editing && item.page.editKey === item.modelData.key
    readonly property bool label: item.modelData.kind === "label"
    readonly property bool sub: item.modelData.kind === "sub"
    readonly property bool info: item.modelData.kind === "info"
    readonly property bool hasSwatch: item.modelData.widget === "hue"
    readonly property string touchKind: item.label || item.info ? "" : item.sub ? "sub" : "row"
    readonly property int touchIndex: item.index
    readonly property var titleRole: item.label ? Ui.sectionLabel : item.sub ? Ui.settingsSub : item.info ? Ui.settingsValue : Ui.settingsItem
    readonly property real titleAlpha: item.label ? Ui.sectionLabel.alpha
        : item.isFocused ? Ui.settingsItem.alpha : Ui.settingsItem.alphaDim
    readonly property real valueAlpha: item.isFocused ? Ui.settingsValueFocused : Ui.settingsValueDim
    readonly property Item titleItem: titleText
    readonly property Item valueItem: valueText
    readonly property Item swatchItem: swatch
    property string shownValue: item.modelData.value
    readonly property bool valueSwapping: valueAnim.running
    property real enter: item.page.arriving ? 0 : 1
    property real shift: item.page.arriving ? Motion.pivotShift : 0
    property int stagger: 0
    property int enterDuration: 0
    property var enterCurve: Motion.strip

    x: Ui.px(item.shift) * (1 - item.enter)
    y: item.page.rowTopOf(item.index)
    width: item.page.flick.width
    height: item.page.rowHeightOf(item.modelData)
    opacity: item.enter

    Component.onCompleted: {
        const old = item.page.shownValues[item.modelData.key]
        item.page.shownValues[item.modelData.key] = item.modelData.value
        if (old === undefined || old === item.modelData.value || !item.page.live)
            return
        item.shownValue = old
        valueAnim.start()
    }

    Connections {
        target: item.page

        function onEntrance(shift, stagger, cap, duration, stripCurve, from) {
            if (item.index < from)
                return
            enterAnim.stop()
            item.shift = shift
            item.stagger = Math.min(cap, item.index - from) * stagger
            item.enterDuration = duration
            item.enterCurve = stripCurve ? Motion.strip : Motion.decel
            item.enter = 0
            enterAnim.start()
        }

        function onSettle() {
            enterAnim.stop()
            item.enter = 1
        }
    }

    SequentialAnimation {
        id: enterAnim

        PauseAnimation {
            duration: item.stagger
        }

        NumberAnimation {
            target: item
            property: "enter"
            from: 0
            to: 1
            duration: item.enterDuration
            easing.type: Motion.bezier
            easing.bezierCurve: item.enterCurve
        }
    }

    SequentialAnimation {
        id: valueAnim

        NumberAnimation {
            target: valueText
            property: "opacity"
            to: Motion.valueDip
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
        ScriptAction {
            script: {
                item.shownValue = item.modelData.value
                item.page.shownValues[item.modelData.key] = item.modelData.value
            }
        }
        NumberAnimation {
            target: valueText
            property: "opacity"
            to: 1
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
    }

    Text {
        id: titleText

        x: Ui.px(Ui.hubEdge)
        y: item.label
            ? item.height - Ui.px(Ui.settingsLabelBaselineInset) - titleText.baselineOffset
            : (item.height - titleText.height) / 2
        width: swatch.visible ? swatch.x - Ui.px(Ui.gutter) - titleText.x
            : Math.min(titleText.implicitWidth, item.width - Ui.px(Ui.hubEdge) - titleText.x)
        text: item.modelData.title
        color: Ui.white(item.titleAlpha)
        font: Ui.font(item.titleRole)
        clip: true

        Behavior on color {
            enabled: item.page.live
            ColorAnimation {
                duration: Motion.focusAlpha
            }
        }
    }

    Text {
        id: valueText

        readonly property real room: item.width - Ui.px(Ui.hubEdge) - titleText.x - titleText.width - Ui.px(Ui.gutter)

        visible: item.shownValue !== "" && !item.hasSwatch
        x: item.width - Ui.px(Ui.hubEdge) - valueText.width
        width: Math.max(0, Math.min(valueText.implicitWidth, valueText.room))
        clip: true
        anchors.baseline: titleText.baseline
        text: item.shownValue
        color: item.editing ? Ui.accent : Ui.white(item.valueAlpha)
        font: Ui.font(Ui.settingsValue)

        Behavior on color {
            enabled: item.page.live
            ColorAnimation {
                duration: Motion.focusAlpha
            }
        }
    }

    Rectangle {
        id: swatch

        visible: item.hasSwatch
        x: item.width - Ui.px(Ui.hubEdge) - swatch.width
        anchors.verticalCenter: parent.verticalCenter
        width: Ui.px(Ui.settingsSwatch)
        height: Ui.px(Ui.settingsSwatch)
        color: item.modelData.swatch !== "" ? item.modelData.swatch
            : Ui.accentForHue(item.modelData.hue < 0 ? Ui.accentHueDefault : item.modelData.hue)
        opacity: item.valueAlpha / Ui.settingsValueFocused
    }
}
