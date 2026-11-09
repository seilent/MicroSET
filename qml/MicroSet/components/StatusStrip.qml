pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string clock: ""
    property int battery: 100
    property bool batteryPresent: true
    property bool bluetooth: false
    property bool playing: false
    property bool paused: false
    property bool percent: false
    readonly property real glyphSize: Ui.px(Ui.statusHeight)
    readonly property real stripTop: Ui.px(Ui.statusTop)
    readonly property bool batteryLow: root.battery < Ui.batteryLowPercent
    readonly property color batteryColor: root.batteryLow ? Ui.accent : Ui.textFull
    property alias row: row
    property alias clockText: clockText
    property alias playGlyph: playGlyph
    property alias bluetoothGlyph: bluetoothGlyph
    property alias batteryGlyph: batteryGlyph
    property alias percentText: percentText

    implicitHeight: root.stripTop + root.glyphSize

    Row {
        id: row

        y: root.stripTop
        anchors.right: root.right
        anchors.rightMargin: Ui.px(Ui.edge)
        height: root.glyphSize
        spacing: Ui.px(Ui.statusGap)

        Text {
            id: clockText

            anchors.verticalCenter: parent.verticalCenter
            visible: root.clock !== ""
            text: root.clock
            color: Ui.white(Ui.time.alpha)
            font: Ui.font(Ui.time)
        }

        Glyph {
            id: playGlyph

            size: Ui.statusHeight
            visible: root.playing || root.paused
            name: root.playing ? "play" : "pause"
            color: Ui.textFull
        }

        Glyph {
            id: bluetoothGlyph

            size: Ui.statusHeight
            visible: root.bluetooth
            name: "bluetooth"
            color: Ui.textFull
        }

        Text {
            id: percentText

            anchors.verticalCenter: parent.verticalCenter
            visible: root.percent && root.batteryPresent && root.battery >= 0
            text: root.battery + "%"
            color: Ui.white(Ui.time.alpha)
            font: Ui.font(Ui.time)
        }

        Glyph {
            id: batteryGlyph

            size: Ui.statusHeight
            visible: root.batteryPresent
            name: "battery"
            level: root.battery / 100
            color: root.batteryColor
        }
    }
}
