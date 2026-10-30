pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool screenOff: false
    property string letter: ""
    readonly property bool shown: root.opacity > 0
    readonly property bool animating: fadeAnim.running
    readonly property int holdDuration: Motion.jumpHold
    readonly property int fadeDuration: Motion.jumpFade
    property alias text: letterText
    property alias hold: holdTimer
    property alias fadeAnimation: fadeAnim

    opacity: 0
    visible: root.opacity > 0 && !root.screenOff

    Text {
        id: letterText

        anchors.centerIn: parent
        text: root.letter
        color: Ui.white(Ui.jumpLetter.alpha)
        font: Ui.font(Ui.jumpLetter)
    }

    Timer {
        id: holdTimer

        interval: Motion.jumpHold
        repeat: false
        onTriggered: fadeAnim.start()
    }

    NumberAnimation {
        id: fadeAnim

        target: root
        property: "opacity"
        from: 1
        to: 0
        duration: Motion.jumpFade
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
    }

    function show(letter: string) {
        root.letter = letter
        if (root.screenOff)
            return
        fadeAnim.stop()
        root.opacity = 1
        holdTimer.restart()
    }

    function dismiss() {
        fadeAnim.stop()
        holdTimer.stop()
        root.opacity = 0
    }

    function complete() {
        if (fadeAnim.running)
            fadeAnim.complete()
    }

    onScreenOffChanged: {
        if (root.screenOff)
            root.dismiss()
    }
}
