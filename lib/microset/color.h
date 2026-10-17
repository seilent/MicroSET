#pragma once

#include <QtGlobal>

namespace MicroSet {

inline uint hueColor(int hue, int saturation)
{
    const int h = ((hue % 360) + 360) % 360;
    const int s = qBound(0, saturation, 100);
    const int k = h / 60;
    const int f = h % 60;
    const int lo = (255 * (100 - s) + 50) / 100;
    const int span = 255 - lo;
    const int up = lo + (span * f + 30) / 60;
    const int down = lo + (span * (60 - f) + 30) / 60;
    const auto rgb = [](int r, int g, int b) { return uint(r) << 16 | uint(g) << 8 | uint(b); };
    switch (k) {
    case 0:
        return rgb(255, up, lo);
    case 1:
        return rgb(down, 255, lo);
    case 2:
        return rgb(lo, 255, up);
    case 3:
        return rgb(lo, down, 255);
    case 4:
        return rgb(up, lo, 255);
    default:
        return rgb(255, lo, down);
    }
}

inline uint scale(uint rgb, quint64 num, quint64 den)
{
    if (den == 0)
        return 0;
    const auto channel = [num, den](uint c) { return uint((2 * c * num + den) / (2 * den)); };
    return channel(rgb >> 16 & 0xff) << 16 | channel(rgb >> 8 & 0xff) << 8 | channel(rgb & 0xff);
}

}
