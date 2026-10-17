pragma Singleton
import QtQuick

QtObject {
    id: ui

    property real surfaceW: 640
    property real surfaceH: 960
    property int turn: 0
    property bool orientationActive: false
    property int landscapeTurn: Env.landscapeTurn
    readonly property real windowW: ui.turn % 2 === 1 ? ui.surfaceH : ui.surfaceW
    readonly property real windowH: ui.turn % 2 === 1 ? ui.surfaceW : ui.surfaceH
    property real userScale: 1.0
    readonly property var padDirections: ["up", "right", "down", "left"]
    readonly property var padFaces: ["y", "b", "a", "x"]
    readonly property int padTurn: ui.orientationActive ? ((ui.landscapeTurn - ui.turn) % 4 + 4) % 4 : 0

    function setSurface(w: real, h: real): void {
        ui.surfaceW = w
        ui.surfaceH = h
    }

    function padButton(name: string): string {
        const d = ui.padDirections.indexOf(name)
        if (d >= 0)
            return ui.padDirections[(d + ui.padTurn) % 4]
        const f = ui.padFaces.indexOf(name)
        return f < 0 ? name : ui.padFaces[(f + ui.padTurn) % 4]
    }

    readonly property bool landscape: ui.windowW > ui.windowH
    readonly property real baseU: ui.landscape
        ? Math.min(ui.windowW / 480, ui.windowH / 272)
        : Math.min(ui.windowW / 272, ui.windowH / 480)
    readonly property real u: ui.baseU * ui.userScale
    readonly property real canvasW: ui.windowW / ui.u
    readonly property real canvasH: ui.windowH / ui.u
    readonly property real minU: ui.landscape
        ? Math.min(ui.windowW / 480, ui.windowH / 260)
        : Math.min(ui.windowW / 272, ui.windowH / 420)
    readonly property real sMax: Math.floor(ui.minU / ui.baseU * 10 + 1e-6) / 10
    readonly property real sMin: 0.8
    readonly property real sStep: 0.1
    readonly property real minCanvasW: ui.landscape ? 480 : 272
    readonly property real minCanvasH: ui.landscape ? 260 : 420

    readonly property real minTextPx: 12

    function px(v: real): real {
        return v * ui.u
    }

    function fontPx(v: real): real {
        return Math.round(Math.max(ui.minTextPx, v * ui.u))
    }

    readonly property string fontDir: Env.fontDir
    readonly property string family: "Selawik"
    readonly property FontLoader light: FontLoader { source: "file://" + ui.fontDir + "/selawkl.ttf" }
    readonly property FontLoader semilight: FontLoader { source: "file://" + ui.fontDir + "/selawksl.ttf" }
    readonly property FontLoader regular: FontLoader { source: "file://" + ui.fontDir + "/selawk.ttf" }
    readonly property FontLoader semibold: FontLoader { source: "file://" + ui.fontDir + "/selawksb.ttf" }
    readonly property FontLoader bold: FontLoader { source: "file://" + ui.fontDir + "/selawkb.ttf" }
    readonly property bool fontsReady: ui.light.status === FontLoader.Ready
        && ui.semilight.status === FontLoader.Ready
        && ui.regular.status === FontLoader.Ready
        && ui.semibold.status === FontLoader.Ready
        && ui.bold.status === FontLoader.Ready

    readonly property real tracking: -0.5
    readonly property real trackingHeader: -1.0

    readonly property var hubHeader: ({ name: "hubHeader", size: 128, style: "Light", caps: Font.AllLowercase, alpha: 0.4, tracking: -1.0, cropped: true, sample: "music" })
    readonly property var homeItem: ({ name: "homeItem", size: 34, style: "Light", caps: Font.AllLowercase, alpha: 1.0, alphaDim: 0.6, tracking: -0.5, sample: "music" })
    readonly property var homeSubtitle: ({ name: "homeSubtitle", size: 8, style: "Regular", caps: Font.AllUppercase, alpha: 0.6, tracking: -0.5, sample: "artists albums songs" })
    readonly property var quickplayHeader: ({ name: "quickplayHeader", size: 34, style: "Light", caps: Font.MixedCase, alpha: 0.6, tracking: -0.5, sample: "Pins" })
    readonly property var quickplayHint: ({ name: "quickplayHint", size: 11, style: "Bold", caps: Font.AllUppercase, alpha: 1.0, tracking: -0.5, sample: "press x on an item" })
    readonly property var pivotActive: ({ name: "pivotActive", size: 22, style: "Bold", caps: Font.AllUppercase, alpha: 1.0, tracking: -0.5, sample: "albums" })
    readonly property var pivotInactive: ({ name: "pivotInactive", size: 22, style: "Light", caps: Font.AllUppercase, alpha: 0.4, tracking: -0.5, sample: "songs" })
    readonly property var sectionLabel: ({ name: "sectionLabel", size: 8, style: "Regular", caps: Font.AllUppercase, alpha: 0.6, tracking: -0.5, sample: "in collection" })
    readonly property var listPrimary: ({ name: "listPrimary", size: 14, style: "Regular", caps: Font.MixedCase, alpha: 1.0, alphaDim: 0.85, tracking: -0.5, sample: "Harmonics vol.2" })
    readonly property var listPlaylist: ({ name: "listPlaylist", size: 14, style: "Bold", caps: Font.AllUppercase, alpha: 1.0, alphaDim: 0.85, tracking: -0.5, sample: "now playing" })
    readonly property var listSecondary: ({ name: "listSecondary", size: 11, style: "Light", caps: Font.MixedCase, alpha: 0.6, tracking: -0.5, sample: "Artist name" })
    readonly property var listCount: ({ name: "listCount", size: 11, style: "Light", caps: Font.AllUppercase, alpha: 0.6, tracking: -0.5, sample: "749 songs" })
    readonly property var detailName: ({ name: "detailName", size: 11, style: "Bold", caps: Font.AllUppercase, alpha: 1.0, tracking: -0.5, sample: "kid cudi" })
    readonly property var detailTitle: ({ name: "detailTitle", size: 13, style: "Bold", caps: Font.AllUppercase, alpha: 1.0, tracking: -0.5, sample: "album title" })
    readonly property var detailSub: ({ name: "detailSub", size: 9, style: "Light", caps: Font.AllUppercase, alpha: 0.6, tracking: -0.5, sample: "2024" })
    readonly property var gridCaption: ({ name: "gridCaption", size: 9, style: "Regular", caps: Font.AllUppercase, alpha: 0.6, tracking: -0.5, sample: "album title" })
    readonly property var letterGlyph: ({ name: "letterGlyph", size: 20, style: "Regular", caps: Font.AllLowercase, alpha: 1.0, tracking: -0.5, sample: "a" })
    readonly property var settingsItem: ({ name: "settingsItem", size: 17, style: "Bold", caps: Font.AllUppercase, alpha: 1.0, alphaDim: 0.6, tracking: -0.5, sample: "output" })
    readonly property var settingsValue: ({ name: "settingsValue", size: 11, style: "Light", caps: Font.AllUppercase, alpha: 0.6, tracking: -0.5, sample: "speaker" })
    readonly property var settingsSub: ({ name: "settingsSub", size: 11, style: "Light", caps: Font.MixedCase, alpha: 1.0, alphaDim: 0.6, tracking: -0.5, sample: "/home/micro/Music" })
    readonly property var npArtist: ({ name: "npArtist", size: 20, style: "Bold", caps: Font.AllUppercase, alpha: 1.0, tracking: -0.5, sample: "artist" })
    readonly property var npAlbum: ({ name: "npAlbum", size: 20, style: "Light", caps: Font.AllUppercase, alpha: 0.85, tracking: -0.5, sample: "album" })
    readonly property var npTitle: ({ name: "npTitle", size: 26, style: "Light", caps: Font.MixedCase, alpha: 1.0, tracking: -0.5, sample: "Song title" })
    readonly property var npNext: ({ name: "npNext", size: 13, style: "Light", caps: Font.MixedCase, alpha: 0.6, tracking: -0.5, sample: "Next track" })
    readonly property var npCount: ({ name: "npCount", size: 13, style: "Light", caps: Font.AllUppercase, alpha: 0.6, tracking: -0.5, sample: "4 of 11" })
    readonly property var time: ({ name: "time", size: 13, style: "Regular", caps: Font.MixedCase, alpha: 0.85, tracking: -0.5, sample: "0:05  -3:48" })
    readonly property var overlayNumber: ({ name: "overlayNumber", size: 48, style: "Light", caps: Font.MixedCase, alpha: 0.6, tracking: -0.5, sample: "13" })
    readonly property var overlayLabel: ({ name: "overlayLabel", size: 7, style: "Bold", caps: Font.AllUppercase, alpha: 0.6, tracking: -0.5, sample: "volume" })
    readonly property var toastArtist: ({ name: "toastArtist", size: 10, style: "Bold", caps: Font.AllUppercase, alpha: 1.0, tracking: -0.5, sample: "artist" })
    readonly property var toastTitle: ({ name: "toastTitle", size: 14, style: "Light", caps: Font.MixedCase, alpha: 1.0, tracking: -0.5, sample: "Song title" })
    readonly property var saverClock: ({ name: "saverClock", size: 50, style: "Light", caps: Font.MixedCase, alpha: 1.0, tracking: -0.5, sample: "7:13" })
    readonly property var saverTitle: ({ name: "saverTitle", size: 32, style: "Light", caps: Font.MixedCase, alpha: 1.0, tracking: -0.5, sample: "Song title" })
    readonly property var saverArtist: ({ name: "saverArtist", size: 20, style: "Light", caps: Font.MixedCase, alpha: 0.85, tracking: -0.5, sample: "Artist" })
    readonly property var saverAlbum: ({ name: "saverAlbum", size: 16, style: "Light", caps: Font.MixedCase, alpha: 0.6, tracking: -0.5, sample: "Album" })
    readonly property var jumpLetter: ({ name: "jumpLetter", size: 120, style: "Light", caps: Font.AllLowercase, alpha: 1.0, tracking: -0.5, sample: "h" })
    readonly property var dialogTitle: ({ name: "dialogTitle", size: 22, style: "Light", caps: Font.AllLowercase, alpha: 1.0, tracking: -0.5, sample: "power" })
    readonly property var dialogItem: ({ name: "dialogItem", size: 17, style: "Bold", caps: Font.AllUppercase, alpha: 1.0, alphaDim: 0.6, tracking: -0.5, sample: "screen off" })
    readonly property var watermark: ({ name: "watermark", size: 14, style: "Light", caps: Font.AllLowercase, alpha: 0.08, tracking: -0.5, sample: "microset" })

    readonly property var roleList: [
        ui.hubHeader, ui.homeItem, ui.homeSubtitle, ui.quickplayHeader, ui.quickplayHint, ui.pivotActive, ui.pivotInactive,
        ui.sectionLabel, ui.listPrimary, ui.listPlaylist, ui.listSecondary, ui.listCount, ui.detailName, ui.detailTitle,
        ui.detailSub, ui.gridCaption, ui.letterGlyph, ui.settingsItem, ui.settingsValue, ui.settingsSub, ui.npArtist,
        ui.npAlbum, ui.npTitle, ui.npNext, ui.npCount, ui.time, ui.overlayNumber, ui.overlayLabel, ui.toastArtist,
        ui.toastTitle, ui.saverClock, ui.saverTitle, ui.saverArtist, ui.saverAlbum, ui.jumpLetter,
        ui.dialogTitle, ui.dialogItem, ui.watermark
    ]

    function letterSpacing(role) {
        return role.tracking * ui.u
    }

    function font(role) {
        return Qt.font({
            family: ui.family,
            styleName: role.style,
            pixelSize: ui.fontPx(role.size),
            letterSpacing: role.tracking * ui.u,
            capitalization: role.caps
        })
    }

    readonly property color bg: "#111111"
    readonly property color bgElevated: "#181818"
    readonly property color tile: "#202020"
    readonly property color tilePressed: "#2A2A2A"
    readonly property color border: "#2C2C2C"
    readonly property real alphaFull: 1.0
    readonly property real alphaHigh: 0.85
    readonly property real alphaMid: 0.6
    readonly property real alphaLow: 0.4
    readonly property real alphaFaint: 0.08
    readonly property color textFull: Qt.rgba(1, 1, 1, 1.0)
    readonly property color textHigh: Qt.rgba(1, 1, 1, 0.85)
    readonly property color textMid: Qt.rgba(1, 1, 1, 0.6)
    readonly property color textLow: Qt.rgba(1, 1, 1, 0.4)
    readonly property color textFaint: Qt.rgba(1, 1, 1, 0.08)
    readonly property color scrimSheet: Qt.rgba(17 / 255, 17 / 255, 17 / 255, 0.55)
    readonly property color scrimOverlay: Qt.rgba(0, 0, 0, 0.8)
    readonly property color black: "#000000"
    readonly property real dimAlpha: 0.6
    readonly property var backdropStops: [[0.0, 0.15], [0.55, 0.35], [1.0, 0.75]]
    readonly property int accentHueDefault: 350
    readonly property int accentHueStep: 10
    readonly property int accentHueCount: 36
    readonly property real accentValue: 0.98
    readonly property int accentSatMax: 83
    readonly property int accentSatMin: 40
    readonly property real accentLumFloor: 0.149
    readonly property real accentBrightSat: 0.84
    property color accent: ui.accentForHue(ui.accentHueDefault)
    property color accentBright: ui.brightForHue(ui.accentHueDefault)

    function hueOnGrid(h: real): int {
        const n = Math.round(Number(h) / ui.accentHueStep) % ui.accentHueCount
        return (n + ui.accentHueCount) % ui.accentHueCount * ui.accentHueStep
    }

    function stepHue(h: int, delta: int): int {
        return ui.hueOnGrid(ui.hueOnGrid(h) + delta * ui.accentHueStep)
    }

    function hsvChannels(h: real, s: real, v: real): var {
        const hh = ((h % 360) + 360) % 360 / 60
        const i = Math.floor(hh) % 6
        const f = hh - Math.floor(hh)
        const p = v * (1 - s)
        const q = v * (1 - s * f)
        const t = v * (1 - s * (1 - f))
        const rgb = i === 0 ? [v, t, p] : i === 1 ? [q, v, p] : i === 2 ? [p, v, t]
            : i === 3 ? [p, q, v] : i === 4 ? [t, p, v] : [v, p, q]
        return [Math.round(255 * rgb[0]), Math.round(255 * rgb[1]), Math.round(255 * rgb[2])]
    }

    function linear(c: int): real {
        const x = c / 255
        return x <= 0.04045 ? x / 12.92 : Math.pow((x + 0.055) / 1.055, 2.4)
    }

    function luminance(ch: var): real {
        return 0.2126 * ui.linear(ch[0]) + 0.7152 * ui.linear(ch[1]) + 0.0722 * ui.linear(ch[2])
    }

    function accentSaturation(h: int): real {
        for (let k = ui.accentSatMax; k > ui.accentSatMin; k--) {
            if (ui.luminance(ui.hsvChannels(h, k / 100, ui.accentValue)) >= ui.accentLumFloor)
                return k / 100
        }
        return ui.accentSatMin / 100
    }

    function channelsColor(ch: var): color {
        return Qt.rgba(ch[0] / 255, ch[1] / 255, ch[2] / 255, 1)
    }

    function accentForHue(h: int): color {
        const hue = ui.hueOnGrid(h)
        return ui.channelsColor(ui.hsvChannels(hue, ui.accentSaturation(hue), ui.accentValue))
    }

    function brightForHue(h: int): color {
        const hue = ui.hueOnGrid(h)
        return ui.channelsColor(ui.hsvChannels(hue, ui.accentBrightSat * ui.accentSaturation(hue), 1.0))
    }
    readonly property real focusBorder: 2
    readonly property real focusBarWidth: 2
    readonly property real focusBarOffset: 8

    function white(alpha: real): color {
        return Qt.rgba(1, 1, 1, alpha)
    }

    function bgAlpha(alpha: real): color {
        return Qt.rgba(17 / 255, 17 / 255, 17 / 255, alpha)
    }

    readonly property real edge: 16
    readonly property real hubEdge: 20
    readonly property real homeTextX: 70
    readonly property real homePitch: 55
    readonly property real homeFirstBaseline: 48
    readonly property real homeSubtitleX: 10
    readonly property real homeSubtitleDrop: 8
    readonly property real homePitchLandscape: 48
    readonly property real qpPeekScale: 0.30
    readonly property real qpPeekY: 130
    readonly property real qpPeekAlpha: 0.4
    readonly property real qpPeekW: ui.qpW * ui.qpPeekScale
    readonly property real headerBand: 70
    readonly property real headerXHeightBottom: 66
    readonly property real pivotTop: ui.landscape ? 58 : 74
    readonly property real pivotBottom: ui.landscape ? 86 : 106
    readonly property real pivotBaseline: ui.pivotTop + 21
    readonly property real pivotGap: 16
    readonly property real pivotLeadGap: ui.hubEdge + 2
    readonly property real contentTop: ui.landscape ? 98 : 128
    readonly property real tileS: ui.landscape ? 60 : Math.round(60 * ui.canvasW / 272)
    readonly property real gutter: 12
    readonly property int gridCols: ui.landscape
        ? Math.floor((ui.canvasW - 2 * ui.hubEdge + ui.gutter) / (ui.tileS + ui.gutter))
        : 3
    readonly property real tileCaptionDrop: 11
    readonly property real tileCaptionPitch: 11
    readonly property real tileCaptionBand: 24
    readonly property real tileCellH: ui.tileS + ui.tileCaptionBand
    readonly property real tileL: 116
    readonly property real tileLGutter: 8
    readonly property real letterTileBorder: 1
    readonly property real letterInsetX: 6
    readonly property real letterInsetBaseline: 8
    readonly property real rowHeight: 40
    readonly property real rowTextInset: 4
    readonly property real rowRing: 20
    readonly property real songIndent: 24
    readonly property real playingDot: 4
    readonly property real playingDotOffset: 6
    readonly property real detailRow: 124
    readonly property real detailNameDrop: 15
    readonly property real detailNameBaseline: ui.pivotBaseline + ui.detailNameDrop
    readonly property real detailRingTop: 150
    readonly property real detailRing: 32
    readonly property real detailRingGap: 24
    readonly property int detailRingCount: 3
    readonly property real detailRingInset: 8
    readonly property real detailLabelDrop: 12
    readonly property real detailLabelBaseline: ui.landscape ? ui.contentTop + ui.capHeight * ui.sectionLabel.size : 200
    readonly property real detailRowsTop: ui.detailLabelBaseline + ui.detailLabelDrop
    readonly property real detailColumn: 200
    readonly property real detailColumnRight: 216
    readonly property real detailListX: ui.landscape ? ui.detailColumnRight - ui.hubEdge : 0
    readonly property real detailTextX: ui.hubEdge + ui.tileL + ui.tileLGutter
    readonly property real detailTitleDrop: 28
    readonly property real detailTitlePitch: 15
    readonly property int detailTitleLines: 2
    readonly property real detailSubPitch: ui.detailTitle.size
    readonly property real albumArtX: ui.edge
    readonly property real albumArtTop: ui.landscape ? ui.contentTop : ui.npArtTop
    readonly property real albumRowsGap: 12
    readonly property real albumRowsTop: ui.landscape ? ui.contentTop : ui.albumArtTop + ui.tileL + ui.albumRowsGap

    function detailRingX(index: int): real {
        return ui.hubEdge + index * (ui.detailRing + ui.detailRingGap)
    }
    readonly property real settingsPitch: 43
    readonly property real settingsLabelBaselineInset: ui.letterInsetBaseline
    readonly property real settingsSwatch: 12
    readonly property real settingsValueFocused: 0.85
    readonly property real settingsValueDim: 0.4
    readonly property real eqValueGap: 8
    readonly property real queueCountBaseline: ui.pivotBaseline
    readonly property real queueRowsTop: ui.contentTop
    readonly property real backRing: 64
    readonly property real backRingStroke: 4
    readonly property real backRingX: 0
    readonly property real backRingY: -15
    readonly property real touchSlop: 25
    readonly property real flingCap: 2000
    readonly property real flingStop: 1
    readonly property real flingMin: 50
    readonly property real npTextTop: 117
    readonly property real npAlbumBaseline: 141
    readonly property real npFromBaseline: 97
    readonly property real npFromBaselineLandscape: 39
    readonly property real npArtTop: 155
    readonly property real npArt: ui.landscape
        ? Math.min(168, ui.canvasH - 106)
        : Math.min(168, ui.canvasW - 2 * ui.edge, ui.canvasH - 311)
    readonly property real npArtTopLandscape: 72
    readonly property real npColumn: 200
    readonly property real npColumnRight: 216
    readonly property real npArtistBaselineLandscape: 60
    readonly property real npAlbumBaselineLandscape: 84
    readonly property real npTitleBaselineLandscape: 116
    readonly property real npNextBaselineLandscape: 140
    readonly property real npProgressGapLandscape: 8
    readonly property real npProgressY: ui.canvasH - 156
    readonly property real npProgressThick: 2
    readonly property real npTimeDrop: 14
    readonly property real npTitleY: ui.canvasH - 118
    readonly property real npNextIndent: 40
    readonly property real npNextPitch: 18
    readonly property int npNextCount: 3
    readonly property real npRing: 32
    readonly property real npRingStroke: 3
    readonly property real npRingY: ui.canvasH - 40
    readonly property var npRingX: [0.13, 0.5, 0.87]
    readonly property real npBtGlyph: 64
    readonly property int seekStepMs: 10000
    readonly property int seekHoldMs: 400
    readonly property int seekTickMs: 250
    readonly property int seekTickStepMs: 1000
    readonly property int seekEndMarginMs: 1000

    function npRingCenter(index: int): real {
        const span = ui.landscape ? ui.canvasW - ui.npColumnRight - ui.edge : ui.canvasW
        const base = ui.landscape ? ui.npColumnRight : 0
        return base + span * ui.npRingX[index]
    }
    readonly property real statusHeight: 12
    readonly property real statusTop: 10
    readonly property real statusGap: 8
    readonly property real lockBar: 11
    readonly property real saverArt: 56
    readonly property real saverBottomMargin: 24
    readonly property real saverArtY: ui.canvasH - ui.saverBottomMargin - ui.saverArt
    readonly property real saverTextX: ui.edge + ui.saverArt + ui.tileLGutter
    readonly property real saverTitleDrop: ui.capHeight * ui.saverTitle.size
    readonly property real saverArtistPitch: ui.saverArtist.size
    readonly property real saverAlbumPitch: ui.saverAlbum.size
    readonly property real saverClockBaseline: ui.edge + ui.capHeight * ui.saverClock.size
    readonly property real overlayRing: 64
    readonly property real overlayGlyph: 20
    readonly property real overlayGlyphOffset: 80
    readonly property real toastHeight: 40
    readonly property real toastArtistBaseline: 16
    readonly property real toastTitleBaseline: 34
    readonly property int jumpCols: ui.landscape ? 9 : 5
    readonly property real jumpGap: 8
    readonly property real jumpTile: Math.min(
        (ui.canvasW - 2 * ui.edge - (ui.jumpCols - 1) * ui.jumpGap) / ui.jumpCols,
        (ui.canvasH - 2 * ui.edge - (ui.jumpRows - 1) * ui.jumpGap) / ui.jumpRows)
    readonly property int jumpExitCells: 2
    readonly property int jumpCells: 38
    readonly property int jumpRows: Math.ceil((ui.jumpExitCells + ui.jumpCells) / ui.jumpCols)
    readonly property real jumpGridH: ui.jumpRows * ui.jumpTile + (ui.jumpRows - 1) * ui.jumpGap
    readonly property real jumpTop: Math.max(ui.edge, (ui.canvasH - ui.jumpGridH) / 2)
    readonly property real stickDeadzone: 0.5
    readonly property int stickRepeatFirst: 300
    readonly property int stickRepeat: 80
    readonly property real glyphBox: 32
    readonly property real glyphStroke: 4
    readonly property real ringStrokeFocused: 4
    readonly property real ringStrokeIdle: 3
    readonly property real ringArcStart: -90

    readonly property real overlayPlusY: 72
    readonly property real overlayNumberBaseline: 160
    readonly property real overlayAnchorH: 480
    readonly property real overlayAnchorScale: ui.landscape ? ui.canvasH / ui.overlayAnchorH : 1
    readonly property real overlayRingY: 216 * ui.overlayAnchorScale
    readonly property real overlayMinusY: 372 * ui.overlayAnchorScale
    readonly property real overlayBottomMargin: 24
    readonly property real overlayTextPitch: 18
    readonly property real overlayLit: 1.0
    readonly property real overlayDim: 0.4
    readonly property real batteryLowPercent: 10

    readonly property real capHeight: 0.70
    readonly property real xHeight: 0.476
    readonly property real qpEdge: 60
    readonly property real qpTile: 77
    readonly property real qpGutter: 2
    readonly property real qpW: ui.qpEdge + 2 * ui.qpTile + ui.qpGutter
    readonly property real qpFirstBaseline: 35
    readonly property real qpContentDrop: 5
    readonly property real qpSectionGap: 70
    readonly property real qpHintDrop: 16
    readonly property real qpHintPitch: 11
    readonly property real qpHintW: 140
    readonly property real qpTilePad: 5
    readonly property real homeMenuXLandscape: 240
    readonly property real homeBarX: ui.homeTextX - ui.focusBarOffset
    readonly property real homeBarTop: ui.capHeight * ui.homeItem.size
    readonly property real homeBarH: ui.homeBarTop + ui.homeSubtitleDrop + ui.capHeight * ui.homeSubtitle.size
}
