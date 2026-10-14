pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: scroll

    property Flickable flick: null
    property var limit: () => Math.max(0, scroll.flick.contentHeight - scroll.flick.height)
    property real velocity: 0
    property real startY: 0
    property real startFinger: 0
    property real lastWrite: 0
    readonly property bool flinging: frames.running
    readonly property real decay: 1000 / Motion.flingTick * -Math.log(Motion.flingRetention)

    signal moved()

    function write(y: real): bool {
        const end = Math.max(0, scroll.limit())
        const target = Math.max(0, Math.min(end, y))
        scroll.lastWrite = target
        scroll.flick.contentY = target
        scroll.moved()
        return target === y
    }

    function begin(fingerY: real) {
        scroll.stop()
        if (typeof scroll.flick.stopScroll === "function")
            scroll.flick.stopScroll()
        scroll.startY = scroll.flick.contentY
        scroll.startFinger = fingerY
        scroll.lastWrite = scroll.flick.contentY
    }

    function dragTo(fingerY: real) {
        scroll.write(scroll.startY - (fingerY - scroll.startFinger))
    }

    function release(v: real) {
        scroll.stop()
        if (Math.abs(v) < Ui.px(Ui.flingStop))
            return
        scroll.velocity = v
        scroll.lastWrite = scroll.flick.contentY
        frames.start()
    }

    function stop() {
        frames.stop()
        scroll.velocity = 0
    }

    function step(seconds: real) {
        const dt = Math.min(seconds, Motion.flingMaxStep / 1000)
        const f = Math.pow(Motion.flingRetention, dt * 1000 / Motion.flingTick)
        const inside = scroll.write(scroll.flick.contentY + scroll.velocity * (1 - f) / scroll.decay)
        scroll.velocity *= f
        if (!inside || Math.abs(scroll.velocity) < Ui.px(Ui.flingStop))
            scroll.stop()
    }

    FrameAnimation {
        id: frames

        onTriggered: scroll.step(frames.frameTime)
    }
}
