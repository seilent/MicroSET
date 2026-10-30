.pragma library

var box = 32
var stroke = 4

var glyphs = {
    back: { fill: false, d: ["M", 28, 16, "L", 5, 16, "M", 14, 7, "L", 5, 16, "L", 14, 25] },
    play: { fill: true, d: ["M", 9, 5, "L", 27, 16, "L", 9, 27, "Z"] },
    pause: { fill: true, d: ["M", 7, 5, "H", 13, "V", 27, "H", 7, "Z", "M", 19, 5, "H", 25, "V", 27, "H", 19, "Z"] },
    next: { fill: true, d: ["M", 5, 5, "L", 21, 16, "L", 5, 27, "Z", "M", 23, 5, "H", 27, "V", 27, "H", 23, "Z"] },
    previous: { fill: true, d: ["M", 27, 5, "L", 11, 16, "L", 27, 27, "Z", "M", 5, 5, "H", 9, "V", 27, "H", 5, "Z"] },
    shuffle: { fill: false, d: ["M", 4, 9, "H", 10, "L", 22, 23, "H", 28, "M", 4, 23, "H", 10, "L", 22, 9, "H", 28, "M", 24, 5, "L", 28, 9, "L", 24, 13, "M", 24, 19, "L", 28, 23, "L", 24, 27] },
    repeat: { fill: false, d: ["M", 23, 9, "A", 10, 10, 0, "1", "0", 26, 16, "M", 18, 4, "L", 23, 9, "L", 18, 14] },
    repeatOne: { fill: false, d: ["M", 23, 9, "A", 10, 10, 0, "1", "0", 26, 16, "M", 18, 4, "L", 23, 9, "L", 18, 14, "M", 13, 13, "L", 16, 11, "V", 21] },
    heart: { fill: true, d: ["M", 16, 28, "L", 5.5, 17, "A", 7, 7, 0, "0", "1", 16, 9, "A", 7, 7, 0, "0", "1", 26.5, 17, "Z"] },
    bluetooth: { fill: false, d: ["M", 8, 10, "L", 24, 22, "L", 16, 29, "V", 3, "L", 24, 10, "L", 8, 22] },
    battery: { fill: false, d: ["M", 3, 9, "H", 25, "V", 23, "H", 3, "Z", "M", 27, 13, "V", 19] },
    plus: { fill: false, d: ["M", 16, 4, "V", 28, "M", 4, 16, "H", 28] },
    minus: { fill: false, d: ["M", 4, 16, "H", 28] },
    search: { fill: false, d: ["M", 4, 12, "A", 8, 8, 0, "1", "0", 20, 12, "A", 8, 8, 0, "1", "0", 4, 12, "M", 18, 18, "L", 28, 28] },
    power: { fill: false, d: ["M", 16, 3, "V", 14, "M", 9.5, 9.5, "A", 9.2, 9.2, 0, "1", "0", 22.5, 9.5] },
    mic: { fill: true, d: ["M", 12, 7, "A", 4, 4, 0, "0", "1", 20, 7, "V", 17.5, "A", 4, 4, 0, "0", "1", 12, 17.5, "Z",
                           "M", 8, 12.6, "V", 17.4, "A", 8, 8, 0, "0", "0", 24, 17.4, "V", 12.6, "H", 21.8, "V", 17.4,
                           "A", 5.8, 5.8, 0, "0", "1", 10.2, 17.4, "V", 12.6, "Z",
                           "M", 14.85, 25.3, "V", 27.6, "H", 17.15, "V", 25.3, "Z",
                           "M", 11.5, 27.6, "V", 30, "H", 20.5, "V", 27.6, "Z"] }
}

var micInk = { left: 8, right: 24, top: 3, bottom: 30 }

var batteryLevel = { x: 6, y: 12, w: 16, h: 8 }

function names() {
    return Object.keys(glyphs)
}

function has(name) {
    return glyphs.hasOwnProperty(name)
}

function filled(name) {
    return has(name) ? glyphs[name].fill : false
}

function path(name, k) {
    if (!has(name))
        return ""
    var out = []
    var d = glyphs[name].d
    for (var i = 0; i < d.length; i++) {
        var t = d[i]
        out.push(typeof t === "number" ? String(t * k) : t)
    }
    return out.join(" ")
}
