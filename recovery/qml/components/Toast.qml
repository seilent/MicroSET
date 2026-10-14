pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool screenOff: false
    property string artist: ""
    property string title: ""
    property real progress: 0
    property bool leaving: false
    readonly property string phase: inAnim.running ? "in" : holdTimer.running ? "hold" : outAnim.running ? "out" : "hidden"
    readonly property bool shown: root.phase !== "hidden"
    readonly property bool animating: inAnim.running || outAnim.running || swapAnim.running
    readonly property real travel: Ui.px(Motion.toastTravel)
    readonly property real eased: root.leaving
        ? 1 - Motion.expOut6(1 - root.progress)
        : Motion.expOut6(root.progress)
    readonly property int inDuration: Motion.toastIn
    readonly property int holdDuration: Motion.toastHold
    readonly property int outDuration: Motion.toastOut
    readonly property color stripColor: Ui.scrimOverlay
    property alias strip: strip
    property alias artistText: artistText
    property alias titleText: titleText
    property alias hold: holdTimer
    property alias inAnimation: inAnim
    property alias outAnimation: outAnim

    width: parent ? parent.width : Ui.windowW
    height: Ui.px(Ui.toastHeight)
    visible: root.progress > 0 && !root.screenOff

    Rectangle {
        id: strip

        x: 0
        y: -root.travel * (1 - root.eased)
        width: root.width
        height: root.height
        color: root.stripColor
        opacity: root.eased

        Item {
            id: textClip

            x: Ui.px(Ui.edge)
            y: 0
            width: strip.width - 2 * Ui.px(Ui.edge)
            height: strip.height
            clip: true

            Text {
                id: artistText

                x: 0
                y: Ui.px(Ui.toastArtistBaseline) - artistText.baselineOffset
                text: root.artist
                textFormat: Text.PlainText
                color: Ui.white(Ui.toastArtist.alpha)
                font: Ui.font(Ui.toastArtist)
            }

            Text {
                id: titleText

                x: 0
                y: Ui.px(Ui.toastTitleBaseline) - titleText.baselineOffset
                text: root.title
                textFormat: Text.PlainText
                color: Ui.white(Ui.toastTitle.alpha)
                font: Ui.font(Ui.toastTitle)
            }
        }
    }

    NumberAnimation {
        id: inAnim

        target: root
        property: "progress"
        from: 0
        to: 1
        duration: Motion.toastIn
        easing.type: Motion.linear
        onFinished: holdTimer.restart()
    }

    Timer {
        id: holdTimer

        interval: Motion.toastHold
        repeat: false
        onTriggered: {
            root.leaving = true
            outAnim.start()
        }
    }

    NumberAnimation {
        id: outAnim

        target: root
        property: "progress"
        from: 1
        to: 0
        duration: Motion.toastOut
        easing.type: Motion.linear
    }

    SequentialAnimation {
        id: swapAnim

        property string nextArtist: ""
        property string nextTitle: ""

        NumberAnimation {
            target: textClip
            property: "opacity"
            from: 1
            to: Motion.valueDip
            duration: Motion.valueChange
        }
        ScriptAction {
            script: {
                root.artist = swapAnim.nextArtist
                root.title = swapAnim.nextTitle
            }
        }
        NumberAnimation {
            target: textClip
            property: "opacity"
            from: Motion.valueDip
            to: 1
            duration: Motion.valueChange
        }
    }

    function show(artist: string, title: string) {
        if (root.screenOff) {
            root.artist = artist
            root.title = title
            return
        }
        if (inAnim.running || holdTimer.running) {
            swapAnim.stop()
            swapAnim.nextArtist = artist
            swapAnim.nextTitle = title
            swapAnim.start()
            if (holdTimer.running)
                holdTimer.restart()
            return
        }
        const from = Motion.expOut6Inverse(root.eased)
        outAnim.stop()
        root.artist = artist
        root.title = title
        root.leaving = false
        inAnim.from = from
        inAnim.start()
    }

    function hide() {
        inAnim.stop()
        outAnim.stop()
        swapAnim.stop()
        holdTimer.stop()
        textClip.opacity = 1
        root.leaving = false
        root.progress = 0
    }

    function complete() {
        if (inAnim.running)
            inAnim.complete()
        if (outAnim.running)
            outAnim.complete()
        if (swapAnim.running)
            swapAnim.complete()
    }

    onScreenOffChanged: {
        if (root.screenOff)
            root.hide()
    }
}
