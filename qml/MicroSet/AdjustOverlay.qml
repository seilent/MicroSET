pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    required property Item page
    readonly property var edited: root.page.editing && root.page.focusedRow !== null
        && root.page.focusedRow.pick === "adjust" ? root.page.focusedRow : null
    readonly property bool shown: root.edited !== null
    property var row: null
    property string key: ""
    readonly property bool balance: root.row !== null && root.row.widget === "edit"
    readonly property real current: root.row === null ? 0 : Number(root.row.current)
    readonly property real low: root.row === null || root.row.min === undefined ? -Infinity : Number(root.row.min)
    readonly property real high: root.row === null || root.row.max === undefined ? Infinity : Number(root.row.max)
    readonly property var cols: root.row === null ? [] : root.balance ? [
        {
            num: (root.current > 0 ? -root.current / 2 : 0).toFixed(1),
            label: "left db",
            up: root.current > 0,
            down: root.current >= 0 && root.current < root.high,
            x: Ui.px(Ui.edge + Ui.adjustColumn)
        },
        {
            num: (root.current < 0 ? root.current / 2 : 0).toFixed(1),
            label: "right db",
            up: root.current < 0,
            down: root.current <= 0 && root.current > root.low,
            x: root.width - Ui.px(Ui.edge + Ui.adjustColumn)
        }
    ] : [
        {
            num: root.current.toFixed(root.row.decimals),
            label: root.row.unit,
            up: root.current < root.high,
            down: root.current > root.low,
            x: root.width / 2
        }
    ]
    readonly property real numberBaseline: Ui.px(Ui.overlayRingY)
        + Math.round(Ui.fontPx(Ui.overlayNumber.size) * Ui.adjustNumberDrop)
    readonly property bool linesFit: line1.y + line1.baselineOffset - Ui.px(Ui.capHeight * Ui.toastArtist.size)
        >= Ui.px(Ui.overlayMinusY + Ui.overlayGlyph / 2) + labelMetrics.ascent
    property alias exitBox: exitBox
    property alias exitText: exitText
    property alias columns: columns
    property alias line1: line1
    property alias line2: line2

    visible: root.opacity > 0
    opacity: root.shown ? 1 : 0

    onEditedChanged: {
        if (root.edited === null) {
            root.key = ""
            return
        }
        const opening = root.key !== root.edited.key
        const before = root.cols.map((c) => c.num)
        root.row = root.edited
        root.key = root.edited.key
        if (opening || !root.page.live)
            return
        for (let i = 0; i < root.cols.length; i++) {
            if (before[i] !== root.cols[i].num)
                columns.itemAt(i).pop()
        }
    }

    Behavior on opacity {
        enabled: root.page.live

        NumberAnimation {
            duration: root.shown ? Motion.volumeShow : Motion.volumeFade
            easing.type: root.shown ? Motion.linear : Motion.bezier
            easing.bezierCurve: Motion.decel
        }
    }

    FontMetrics {
        id: labelMetrics

        font: Ui.font(Ui.overlayLabel)
    }

    Rectangle {
        anchors.fill: parent
        color: Ui.scrimOverlay
    }

    Item {
        id: exitBox

        x: 0
        y: 0
        width: Ui.px(Ui.adjustExit)
        height: Ui.px(Ui.adjustExit)

        Text {
            id: exitText

            x: Ui.px(Ui.edge)
            y: Ui.px(Ui.edge)
            text: "exit"
            textFormat: Text.PlainText
            color: Ui.white(Ui.overlayLit)
            font: Ui.font(Ui.overlayLabel)
        }
    }

    Repeater {
        id: columns

        model: root.cols

        delegate: Item {
            id: column

            required property var modelData
            required property int index
            readonly property Item plusBox: plusBox
            readonly property Item minusBox: minusBox
            readonly property Item numberItem: number
            readonly property Item labelItem: label

            function pop(): void {
                popAnim.restart()
            }

            anchors.fill: parent

            Item {
                id: plusBox

                x: column.modelData.x - plusBox.width / 2
                y: Ui.px(Ui.overlayPlusY) - plusBox.height / 2
                width: Ui.px(Ui.overlayRing)
                height: Ui.px(Ui.overlayRing)
                opacity: column.modelData.up ? Ui.overlayLit : Ui.overlayDim
                layer.enabled: true

                Glyph {
                    anchors.centerIn: parent
                    name: "plus"
                    size: Ui.overlayGlyph
                }
            }

            Item {
                id: minusBox

                x: column.modelData.x - minusBox.width / 2
                y: Ui.px(Ui.overlayMinusY) - minusBox.height / 2
                width: Ui.px(Ui.overlayRing)
                height: Ui.px(Ui.overlayRing)
                opacity: column.modelData.down ? Ui.overlayLit : Ui.overlayDim
                layer.enabled: true

                Glyph {
                    anchors.centerIn: parent
                    name: "minus"
                    size: Ui.overlayGlyph
                }
            }

            Text {
                id: label

                x: column.modelData.x - label.width / 2
                y: Ui.px(Ui.overlayMinusY + Ui.overlayGlyph / 2)
                text: column.modelData.label
                textFormat: Text.PlainText
                color: Ui.white(Ui.overlayDim)
                font: Ui.font(Ui.overlayLabel)
            }

            Text {
                id: number

                x: root.balance ? column.modelData.x - number.width / 2 : Ui.px(Ui.edge)
                y: root.numberBaseline - number.baselineOffset
                text: column.modelData.num
                textFormat: Text.PlainText
                color: Ui.white(Ui.overlayLit)
                font: Ui.font(Ui.overlayNumber)

                NumberAnimation on scale {
                    id: popAnim

                    running: false
                    from: Motion.toggleScale
                    to: 1
                    duration: Motion.volumePop
                    easing.type: Motion.bezierPop
                }
            }
        }
    }

    Text {
        id: line2

        visible: root.linesFit
        x: Ui.px(Ui.edge)
        y: root.height - Ui.px(Ui.overlayBottomMargin) - line2.height
        width: Math.max(0, root.width - Ui.px(Ui.edge) - line2.x)
        clip: true
        text: root.row === null ? "" : String(root.row.value).toUpperCase()
        textFormat: Text.PlainText
        color: Ui.white(Ui.overlayDim * Ui.toastTitle.alpha)
        font: Ui.font(Ui.toastTitle)
    }

    Text {
        id: line1

        visible: root.linesFit
        x: Ui.px(Ui.edge)
        y: line2.y + line2.baselineOffset - Ui.px(Ui.overlayTextPitch) - line1.baselineOffset
        width: Math.max(0, root.width - Ui.px(Ui.edge) - line1.x)
        clip: true
        text: root.row === null ? "" : root.row.title
        textFormat: Text.PlainText
        color: Ui.white(Ui.overlayDim * Ui.toastArtist.alpha)
        font: Ui.font(Ui.toastArtist)
    }
}
