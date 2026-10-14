pragma Singleton
import QtQuick

QtObject {
    id: motion

    function expOut(t: real, k: real): real {
        return 1 - (Math.exp(k * (1 - t)) - 1) / (Math.exp(k) - 1)
    }

    function expIn(t: real, k: real): real {
        return (Math.exp(k * t) - 1) / (Math.exp(k) - 1)
    }

    function expOut6(t: real): real {
        return 1 - (Math.exp(6 * (1 - t)) - 1) / (Math.exp(6) - 1)
    }

    function expOut6Inverse(v: real): real {
        return 1 - Math.log(1 + (1 - v) * (Math.exp(6) - 1)) / 6
    }

    function expIn6(t: real): real {
        return (Math.exp(6 * t) - 1) / (Math.exp(6) - 1)
    }

    function expIn1(t: real): real {
        return (Math.exp(1 * t) - 1) / (Math.exp(1) - 1)
    }

    function expOut1(t: real): real {
        return 1 - (Math.exp(1 * (1 - t)) - 1) / (Math.exp(1) - 1)
    }

    function sineOut(t: real): real {
        return Math.sin(t * Math.PI / 2)
    }

    function expIn15(t: real): real {
        return (Math.exp(15 * t) - 1) / (Math.exp(15) - 1)
    }

    function lerp(a: real, b: real, t: real): real {
        return a + (b - a) * t
    }

    function outExpo(t: real): real {
        return t >= 1 ? 1 : 1 - Math.pow(2, -10 * t)
    }

    function inQuad(t: real): real {
        return t * t
    }

    readonly property var decel: [0.1, 0.9, 0.2, 1.0, 1, 1]
    readonly property var strip: [0.2, 0.8, 0.2, 1, 1, 1]
    readonly property int sine: Easing.OutSine
    readonly property int pop: Easing.OutExpo
    readonly property int bezierPop: Easing.OutBack
    readonly property int easeIn: Easing.InQuad
    readonly property int easeOut: Easing.OutQuad
    readonly property int linear: Easing.Linear
    readonly property int bezier: Easing.BezierSpline

    readonly property real perspectiveRatio: 2.3

    function perspective(w: real): real {
        return motion.perspectiveRatio * w
    }

    readonly property int turnstileOut: 250
    readonly property int turnstileOutHold: 240
    readonly property int turnstileIn: 350
    readonly property int turnstileInFade: 10
    readonly property real turnstileOutAngle: 50
    readonly property real turnstileInAngle: -80
    readonly property int headerOut: 250
    readonly property real headerOutShift: -0.25
    readonly property int rotateOut: 250
    readonly property int rotateIn: 250

    readonly property int homeEnter: 220
    readonly property int homeEnterPeek: 420
    readonly property int homeEnterStagger: 20
    readonly property real homeEnterShift: 24

    readonly property int homeOut: 250
    readonly property int homeOutStagger: 28
    readonly property real homeOutShift: -0.25

    readonly property int pivotOut: 300
    readonly property int pivotStrip: 380
    readonly property int pivotLabel: 200
    readonly property int pivotRows: 380
    readonly property real pivotShift: 34
    readonly property int pivotRowStagger: 22
    readonly property int pivotRowCap: 10
    readonly property real pivotHeaderParallax: 0.2

    readonly property int quickplay: 420
    readonly property real quickplayAngle: -12
    readonly property real quickplayScale: 0.44
    readonly property real quickplayAlpha: 0.6
    readonly property real quickplayMenuInset: 84
    readonly property real quickplayMenuY: 94

    readonly property int nowPlayingIn: 300
    readonly property real nowPlayingScale: 0.9
    readonly property real nowPlayingOriginX: 0.4
    readonly property real nowPlayingOriginY: 0.5
    readonly property int nowPlayingOut: 250
    readonly property int nowPlayingFade: 300

    readonly property int trackHide: 500
    readonly property int trackShow: 400
    readonly property real trackArtScale: 0.8
    readonly property real trackArtOriginX: 0.2
    readonly property real trackArtOriginY: 0.5
    readonly property real trackShiftOut: 50
    readonly property real trackShiftIn: -20

    readonly property int artistFade: 2000
    readonly property int variantFade: 380

    readonly property int sheet: 380
    readonly property real sheetScrim: 0.55

    readonly property int dialog: 300
    readonly property int dialogHide: 150
    readonly property real dialogScale: 0.9
    readonly property real dialogScrim: 0.8

    readonly property int swivelIn: 350
    readonly property int swivelOut: 250
    readonly property real swivelAngle: 30
    readonly property real swivelDim: 0.4

    readonly property int jumpHold: 350
    readonly property int jumpFade: 380

    readonly property int saver: 380
    readonly property int dim: 380
    readonly property int dimDelay: 7000
    readonly property real dimAlpha: 0.6

    readonly property int focusCursor: 100
    readonly property int focusAlpha: 200
    readonly property int scroll: 100
    readonly property int page: 500
    readonly property int press: 100
    readonly property real pressScale: 0.96
    readonly property int touchReturnDelay: 200
    readonly property real flingRetention: 0.95
    readonly property int flingTick: 16
    readonly property int flingWindow: 100
    readonly property int flingMaxStep: 33
    readonly property int toggle: 400
    readonly property real toggleScale: 0.7
    readonly property int toggleSwap: 200
    readonly property int idle: 5000
    readonly property int idleFade: 380
    readonly property real idleAlpha: 0.4
    readonly property int progress: 250
    readonly property int elapsedTick: 1000
    readonly property int valueChange: 200
    readonly property real valueDip: 0.2
    readonly property var pulse: [[0, 1], [200, 1], [400, 1.4], [600, 0.8], [800, 1]]
    readonly property int volumeShow: 200
    readonly property int volumePop: 400
    readonly property int volumeHide: 1500
    readonly property int volumeFade: 380
    readonly property int toastIn: 350
    readonly property int toastHold: 3500
    readonly property int toastOut: 250
    readonly property real toastTravel: 40
    readonly property int listChange: 220
    readonly property int listChangeStagger: 50
    readonly property int listChangeCap: 10
    readonly property real listChangeShift: -5

    readonly property int backdropScale: 20000
    readonly property int backdropPan: 26000
    readonly property real backdropScaleMax: 1.06
    readonly property real backdropPanAmplitude: 8
    readonly property int saverDrift: 18000
    readonly property real saverDriftFrom: 8
    readonly property real saverDriftTo: -27

    readonly property int inOutSine: Easing.InOutSine
}
