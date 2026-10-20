#include "fakemicrosetd.h"
#include "settingsmodel.h"

#include <QFont>
#include <QFontInfo>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(MicroSetPlugin)
Q_IMPORT_QML_PLUGIN(MicroSetPageTestPlugin)

using namespace Qt::StringLiterals;

class FitTest : public QObject
{
    Q_OBJECT

private slots:
    void sheetWordsFitAt640()
    {
        QVERIFY(qEnvironmentVariableIsSet("MICROSET_FONT_DIR"));
        SettingsModel model;
        model.setFixture(FakeMicroSetd::describe({FakeMicroSetd::shipped(), FakeMicroSetd::fixture(u"50-microdap.json"_s),
                                                  FakeMicroSetd::fixture(u"50-microemu.json"_s)}),
                         {{u"power.gpu_level"_s, u"manual"_s}, {u"display.auto_rotate"_s, false}},
                         {{u"rgb.enabled"_s, true}});
        QQmlApplicationEngine engine;
        engine.setInitialProperties({
            {u"model"_s, QVariant::fromValue(static_cast<QObject *>(&model))},
            {u"screenshot"_s, true},
            {u"shotState"_s, u"none"_s},
            {u"shotWidth"_s, 640},
            {u"shotHeight"_s, 960},
        });
        engine.loadFromModule("MicroSetPageTest", "PageRoot");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        QVERIFY(window);
        QCOMPARE(QFontInfo(QFont(u"Selawik"_s)).family(), u"Selawik"_s);
        QObject *view = window->findChild<QObject *>(u"settingsView"_s);
        QVERIFY(view);
        int checked = 0;
        QStringList misses;
        int titles = 0;
        for (const QVariant &group : model.groups(0)) {
            const QString title = group.toMap().value(u"group"_s).toString();
            if (title.isEmpty())
                continue;
            QVERIFY(QMetaObject::invokeMethod(view, "open", Q_ARG(QString, title)));
            QObject *page = qvariant_cast<QObject *>(qvariant_cast<QObject *>(view->property("stack"))->property("currentItem"));
            auto *sheet = qvariant_cast<QQuickItem *>(page->property("sheet"));
            QVERIFY(sheet);
            for (const QVariant &item : model.rows(title, 0)) {
                const QVariantMap row = item.toMap();
                if (row.value(u"pick"_s).toString() != "sheet"_L1)
                    continue;
                QVERIFY(QMetaObject::invokeMethod(page, "openValue", Q_ARG(QString, row.value(u"key"_s).toString())));
                QVERIFY(sheet->property("shown").toBool());
                auto *panel = qvariant_cast<QQuickItem *>(sheet->property("panel"));
                QTRY_COMPARE(panel->width(), 640.0);
                const qreal room = panel->width() - 2 * sheet->property("edge").toReal();
                const QStringList words = sheet->property("items").toStringList();
                int texts = 0;
                for (QQuickItem *delegate : qvariant_cast<QQuickItem *>(sheet->property("list"))->childItems()) {
                    for (QQuickItem *text : delegate->childItems()) {
                        if (text->implicitWidth() > room)
                            misses << u"%1: %2 %3 > %4"_s.arg(row.value(u"key"_s).toString(), text->property("text").toString(),
                                                              QString::number(text->implicitWidth()), QString::number(room));
                        ++texts;
                    }
                }
                QCOMPARE(texts, words.size());
                for (QQuickItem *child : panel->childItems()) {
                    if (child->property("text").toString() != sheet->property("title").toString())
                        continue;
                    if (child->implicitWidth() > room)
                        misses << u"%1: title %2 %3 > %4"_s.arg(row.value(u"key"_s).toString(), child->property("text").toString(),
                                                                QString::number(child->implicitWidth()), QString::number(room));
                    ++titles;
                }
                checked += texts;
                QVERIFY(QMetaObject::invokeMethod(page, "cancelValue"));
            }
            QVERIFY(QMetaObject::invokeMethod(page, "back"));
        }
        QVERIFY(checked > 200);
        QVERIFY(titles > 30);
        QVERIFY2(misses.isEmpty(),
                 qPrintable(u"sheet words or titles wider than the panel at 640 wide:\n"_s + misses.join(u'\n')));
    }
};

QTEST_MAIN(FitTest)
#include "tst_fit.moc"
