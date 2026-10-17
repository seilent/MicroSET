#include <microset/color.h>

#include <QTest>

using MicroSet::hueColor;
using MicroSet::scale;

class ColorTest : public QObject
{
    Q_OBJECT

private slots:
    void hues()
    {
        QCOMPARE(hueColor(0, 100), 0xff0000u);
        QCOMPARE(hueColor(120, 100), 0x00ff00u);
        QCOMPARE(hueColor(240, 100), 0x0000ffu);
        QCOMPARE(hueColor(60, 100), 0xffff00u);
        QCOMPARE(hueColor(30, 100), 0xff8000u);
        QCOMPARE(hueColor(350, 100), 0xff002bu);
        QCOMPARE(hueColor(0, 40), 0xff9999u);
        for (int hue = 0; hue < 360; hue += 10)
            QCOMPARE(hueColor(hue, 0), 0xffffffu);
    }

    void hueWraps()
    {
        QCOMPARE(hueColor(360, 100), hueColor(0, 100));
        QCOMPARE(hueColor(370, 100), hueColor(10, 100));
        QCOMPARE(hueColor(-10, 100), hueColor(350, 100));
    }

    void saturationClamped()
    {
        QCOMPARE(hueColor(30, 150), hueColor(30, 100));
        QCOMPARE(hueColor(30, -5), 0xffffffu);
    }

    void scaled()
    {
        QCOMPARE(scale(0xffffff, 1000 * 100, 4095 * 100), 0x3e3e3eu);
        QCOMPARE(scale(0xff002b, 3071 * 50, 4095 * 100), 0x600010u);
        QCOMPARE(scale(0xffffff, 123 * 100, 4095 * 100), 0x080808u);
        QCOMPARE(scale(0xffffff, 0, 4095 * 100), 0u);
        QCOMPARE(scale(0xffffff, 100, 0), 0u);
    }
};

QTEST_GUILESS_MAIN(ColorTest)
#include "tst_color.moc"
