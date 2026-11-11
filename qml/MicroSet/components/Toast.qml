pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool screenOff: false
    property string artist: ""
    property string title: ""
    property real progress: 0
    property bool leaving: false
    property bool sticky: false
    property bool topPlace: false
    property bool overlayUp: false
    property real surface: parent ? parent.height : Ui.windowH
    property bool pending: false
    property bool placedTop: false
    readonly property bool atTop: root.placedTop
    readonly property string phase: root.pending || inAnim.running ? "in" : holdTimer.running ? "hold" : outAnim.running ? "out" : "hidden"
    readonly property bool shown: root.phase !== "hidden"
    readonly property bool animating: root.pending || inAnim.running || outAnim.running || swapAnim.running
    readonly property real travel: Ui.px(Motion.toastTravel)
    readonly property real eased: root.leaving
        ? 1 - Motion.expOut6(1 - root.progress)
        : Motion.expOut6(root.progress)
    readonly property int inDuration: root.placedTop ? Motion.toastIn : Motion.toastRise
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
    y: root.placedTop ? 0 : root.surface - root.height
    visible: root.progress > 0 && !root.screenOff

    Rectangle {
        id: strip

        x: 0
        y: (root.placedTop ? -1 : 1) * root.travel * (1 - root.eased)
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
        easing.type: Motion.linear
        onFinished: holdTimer.restart()
    }

    Timer {
        id: holdTimer

        interval: Motion.toastHold
        repeat: false
        onTriggered: {
            if (root.sticky) {
                holdTimer.restart()
                return
            }
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
        if (root.pending) {
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
        if (outAnim.running) {
            const from = Motion.expOut6Inverse(root.eased)
            root.pending = true
            outAnim.stop()
            root.artist = artist
            root.title = title
            root.leaving = false
            inAnim.from = from
            inAnim.duration = Math.round(root.inDuration * (1 - from))
            inAnim.start()
            root.pending = false
            return
        }
        root.artist = artist
        root.title = title
        root.leaving = false
        root.progress = 0
        root.placedTop = root.topPlace
        root.pending = true
        Qt.callLater(root.place)
    }

    function place() {
        if (!root.pending)
            return
        root.placedTop = root.topPlace
        inAnim.from = 0
        inAnim.duration = root.inDuration
        inAnim.start()
        root.pending = false
    }

    function raise() {
        if (root.pending || root.placedTop || root.phase === "hidden")
            return
        if (inAnim.running) {
            const p = root.progress
            root.pending = true
            inAnim.stop()
            root.placedTop = true
            inAnim.from = p
            inAnim.duration = Math.round(Motion.toastIn * (1 - p))
            inAnim.start()
            root.pending = false
            return
        }
        root.placedTop = true
    }

    function hide() {
        root.pending = false
        inAnim.stop()
        outAnim.stop()
        swapAnim.stop()
        holdTimer.stop()
        textClip.opacity = 1
        root.leaving = false
        root.progress = 0
    }

    function complete() {
        if (root.pending)
            root.place()
        if (inAnim.running)
            inAnim.complete()
        if (outAnim.running)
            outAnim.complete()
        if (swapAnim.running)
            swapAnim.complete()
    }

    onStickyChanged: {
        if (root.sticky || !holdTimer.running)
            return
        holdTimer.stop()
        root.leaving = true
        outAnim.start()
    }

    onScreenOffChanged: {
        if (root.screenOff)
            root.hide()
    }

    onOverlayUpChanged: {
        if (root.overlayUp)
            root.raise()
    }
}
