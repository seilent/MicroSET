pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: screen

    property QtObject recovery: null
    property bool animated: true
    readonly property var actions: screen.recovery === null ? [] : screen.recovery.actions
    readonly property string reason: screen.recovery === null ? ""
        : screen.recovery.failed ? screen.recovery.failingLabel + " failed" : ""
    readonly property var journalLines: screen.recovery === null ? [] : screen.recovery.journal
    readonly property bool journalLoaded: screen.recovery !== null && screen.recovery.journalLoaded
    readonly property real linePitch: Math.ceil(lineMetrics.height)
    property bool pressed: false
    property real journalAnchor: 0
    readonly property real journalEnd: Math.max(0, journal.contentHeight - journal.height, screen.journalAnchor)
    readonly property bool dialogUp: dialog.shown || dialog.animating
    property alias list: list
    property alias journal: journal
    property alias dialog: dialog
    property alias toast: toast
    property alias focusBar: focusBar

    function activate(index: int) {
        const a = screen.actions[index]
        if (a === undefined)
            return
        switch (a.key) {
        case "switch":
            screen.recovery.switchTo(a.name, a.title)
            break
        case "rollback":
            screen.confirmRollback()
            break
        case "poweroff":
            screen.recovery.powerOff(a.title)
            break
        }
    }

    function press(name: string) {
        if (name !== "a")
            return
        screen.pressed = true
    }

    function release(name: string) {
        if (name !== "a")
            return
        screen.pressed = false
        if (dialog.pressed)
            dialog.close()
    }

    function confirmRollback() {
        dialog.open()
        dialog.focusIndex = dialog.cancelIndex
    }

    function pageJournal(direction: int): bool {
        journalTouch.stop()
        const end = screen.journalEnd
        const base = journalScroll.running ? journalScroll.to : journal.contentY
        const target = Math.max(0, Math.min(end, base + direction * journal.height))
        journalScroll.stop()
        if (target === journal.contentY)
            return true
        if (!screen.animated) {
            journal.contentY = target
            return true
        }
        journalScroll.from = journal.contentY
        journalScroll.to = target
        journalScroll.start()
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
        switch (name) {
        case "up":
            return list.moveFocus(-1)
        case "down":
            return list.moveFocus(1)
        case "lt":
            return screen.pageJournal(-1)
        case "rt":
            return screen.pageJournal(1)
        case "a":
            if (!repeat)
                screen.activate(list.focusIndex)
            return true
        default:
            return false
        }
    }

    function showJournalEnd() {
        const end = Math.max(0, journal.contentHeight - journal.height)
        let top = end
        for (let i = 0; i < lines.children.length; i++) {
            const line = lines.children[i]
            if (line.visible && line.y >= end - 0.5) {
                top = line.y
                break
            }
        }
        screen.journalAnchor = top
        journal.contentY = top
    }

    Connections {
        target: screen.recovery
        ignoreUnknownSignals: true

        function onActionFailed(action: string, message: string) {
            toast.show(action, message)
        }
    }

    FontMetrics {
        id: lineMetrics

        font: Ui.font(Ui.settingsSub)
    }

    PageSurface {
        anchors.fill: parent
    }

    CroppedHeader {
        id: header

        text: "recovery"
        animated: screen.animated
    }

    Text {
        id: reasonText

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(Ui.pivotBaseline) - reasonText.baselineOffset
        width: screen.width - 2 * Ui.px(Ui.hubEdge)
        visible: screen.reason !== ""
        text: screen.reason
        textFormat: Text.PlainText
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
        clip: true
    }

    FocusList {
        id: list

        x: 0
        y: Ui.px(Ui.contentTop)
        width: screen.width
        height: list.count * list.rowPitch
        rowPitch: Ui.px(Ui.settingsPitch)
        model: screen.actions

        delegate: PressScale {
            id: row

            required property int index
            required property var modelData
            readonly property bool isFocused: row.index === list.focusIndex

            pressed: row.isFocused && screen.pressed
            animated: screen.animated

            width: list.width
            height: list.rowPitch

            Text {
                x: Ui.px(Ui.hubEdge)
                anchors.verticalCenter: parent.verticalCenter
                width: row.width - 2 * Ui.px(Ui.hubEdge)
                text: row.modelData.title
                textFormat: Text.PlainText
                color: Ui.white(row.isFocused ? Ui.dialogItem.alpha : Ui.dialogItem.alphaDim)
                font: Ui.font(Ui.dialogItem)
                clip: true

                Behavior on color {
                    enabled: screen.animated
                    ColorAnimation {
                        duration: Motion.focusAlpha
                    }
                }
            }

            TapHandler {
                enabled: !screen.dialogUp
                onTapped: {
                    list.touchFocus(row.index)
                    screen.activate(row.index)
                }
            }
        }
    }

    FocusBar {
        id: focusBar

        visible: list.count > 0
        rowY: list.y + list.focusY
        rowHeight: list.rowPitch
        opacity: 1 - dialog.scrim.opacity
        animated: screen.animated
    }

    Item {
        id: journalLabel

        x: 0
        y: list.y + list.height
        width: screen.width
        height: Ui.px(Ui.rowHeight)
        visible: screen.recovery !== null && screen.recovery.failingUnit !== ""

        Text {
            id: unitText

            x: Ui.px(Ui.hubEdge)
            y: journalLabel.height - Ui.px(Ui.settingsLabelBaselineInset) - unitText.baselineOffset
            width: journalLabel.width - 2 * Ui.px(Ui.hubEdge)
            text: screen.recovery === null ? "" : screen.recovery.failingUnit
            textFormat: Text.PlainText
            color: Ui.white(Ui.sectionLabel.alpha)
            font: Ui.font(Ui.sectionLabel)
            clip: true
        }
    }

    Flickable {
        id: journal

        x: 0
        y: journalLabel.y + journalLabel.height
        width: screen.width
        height: Math.max(0, Math.floor((screen.height - journal.y) / screen.linePitch) * screen.linePitch)
        contentWidth: journal.width
        contentHeight: lines.height
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        visible: journalLabel.visible

        onHeightChanged: screen.showJournalEnd()
        onContentHeightChanged: screen.showJournalEnd()

        function stopScroll(): void {
            journalScroll.stop()
        }

        TouchScroll {
            id: journalTouch

            flick: journal
            limit: () => screen.journalEnd
        }

        Column {
            id: lines

            x: Ui.px(Ui.hubEdge)
            width: journal.width - 2 * Ui.px(Ui.hubEdge)

            Repeater {
                model: screen.journalLines

                delegate: Text {
                    required property string modelData

                    width: lines.width
                    lineHeightMode: Text.FixedHeight
                    lineHeight: screen.linePitch
                    wrapMode: Text.Wrap
                    text: modelData
                    color: Ui.white(Ui.settingsSub.alphaDim)
                    font: Ui.font(Ui.settingsSub)
                    textFormat: Text.PlainText
                }
            }

            Text {
                width: lines.width
                height: screen.linePitch
                visible: screen.journalLoaded && screen.journalLines.length === 0
                text: "no journal lines"
                color: Ui.white(Ui.settingsSub.alphaDim)
                font: Ui.font(Ui.settingsSub)
            }
        }
    }

    NumberAnimation {
        id: journalScroll

        target: journal
        property: "contentY"
        duration: Motion.page
        easing.type: Motion.easeIn
    }

    Dialog {
        id: dialog

        readonly property int rebootIndex: 0
        readonly property int cancelIndex: 1

        anchors.fill: parent
        title: "previous image"
        items: ["hold to reboot", "cancel"]

        onAccepted: (index) => {
            if (index !== dialog.rebootIndex) {
                dialog.close()
                return
            }
            dialog.pressed = true
            screen.recovery.bootPrevious("boot the previous image")
        }
    }

    MouseArea {
        id: journalDrag

        property bool tapping: false
        property bool dragging: false
        property point start: Qt.point(0, 0)
        property var samples: []

        x: journal.x
        y: journal.y
        width: journal.width
        height: journal.height
        enabled: journal.visible && !screen.dialogUp

        function sample(y: real) {
            const now = Date.now()
            const kept = []
            let old = null
            for (const s of journalDrag.samples) {
                if (s.t < now - Motion.flingWindow)
                    old = s
                else
                    kept.push(s)
            }
            if (old !== null)
                kept.unshift(old)
            kept.push({ t: now, y: y })
            journalDrag.samples = kept
        }

        function velocity(samples: var): real {
            if (samples.length < 2)
                return 0
            const r = samples[samples.length - 1]
            const at = r.t - Motion.flingWindow
            let anchor = samples[0]
            if (anchor.t < at) {
                for (let i = 0; i < samples.length - 1; i++) {
                    const a = samples[i]
                    const b = samples[i + 1]
                    if (a.t <= at && b.t >= at) {
                        const f = b.t > a.t ? (at - a.t) / (b.t - a.t) : 0
                        anchor = { t: at, y: a.y + (b.y - a.y) * f }
                        break
                    }
                }
            }
            const dt = r.t - anchor.t
            if (dt <= 0)
                return 0
            return (r.y - anchor.y) / dt * 1000
        }

        onPressed: (mouse) => {
            journalTouch.stop()
            journalDrag.start = Qt.point(mouse.x, mouse.y)
            journalDrag.samples = []
            journalDrag.tapping = true
            journalDrag.dragging = false
            journalDrag.sample(mouse.y)
        }

        onPositionChanged: (mouse) => {
            journalDrag.sample(mouse.y)
            if (journalDrag.dragging) {
                journalTouch.dragTo(mouse.y)
                return
            }
            if (!journalDrag.tapping)
                return
            const dx = mouse.x - journalDrag.start.x
            const dy = mouse.y - journalDrag.start.y
            const slop = Ui.px(Ui.touchSlop)
            if (dx * dx + dy * dy <= slop * slop)
                return
            journalDrag.tapping = false
            if (Math.abs(dy) < Math.abs(dx))
                return
            journalDrag.dragging = true
            journalTouch.begin(mouse.y)
        }

        onReleased: (mouse) => {
            if (!journalDrag.dragging)
                return
            journalDrag.sample(mouse.y)
            const v = journalDrag.velocity(journalDrag.samples)
            journalDrag.dragging = false
            journalDrag.samples = []
            if (Math.abs(v) >= Ui.px(Ui.flingMin))
                journalTouch.release(-Math.max(-Ui.px(Ui.flingCap), Math.min(Ui.px(Ui.flingCap), v)))
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: screen.dialogUp

        onClicked: (mouse) => {
            const top = Ui.px(Ui.contentTop)
            const index = Math.floor((mouse.y - top) / dialog.pitch)
            const title = dialog.titleText
            const onTitle = mouse.x >= title.x && mouse.x < title.x + title.width
                && mouse.y >= title.y && mouse.y < title.y + title.height
            const kind = mouse.y >= top && index < dialog.items.length ? "item" : onTitle ? "block" : "scrim"
            if (kind === "item" && index === dialog.rebootIndex) {
                dialog.focusIndex = index
                return
            }
            const action = dialog.touchPress(kind, index, mouse.x, null)
            if (action !== "")
                dialog.handleAction(action, false)
        }
    }

    Toast {
        id: toast

        x: 0
        y: 0
        width: screen.width
    }
}
