#include "gamerows.h"

#include <QSignalSpy>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

class GameRowsTest : public QObject
{
    Q_OBJECT

    static QVariantList games(const QStringList &titles)
    {
        QVariantList out;
        for (qsizetype i = 0; i < titles.size(); ++i)
            out << QVariantMap{{u"appid"_s, QString::number(100 + i)}, {u"title"_s, titles.at(i)}};
        return out;
    }

    static QStringList rows(const GameRows &model)
    {
        QStringList out;
        for (int row = 0; row < model.rowCount(); ++row) {
            const QModelIndex index = model.index(row);
            out << model.data(index, GameRows::KindRole).toString() + u' '
                    + model.data(index, GameRows::LetterRole).toString() + u' '
                    + model.data(index, GameRows::TitleRole).toString();
        }
        return out;
    }

    static QStringList titles(const GameRows &model)
    {
        QStringList out;
        for (int row = 0; row < model.rowCount(); ++row)
            if (model.data(model.index(row), GameRows::KindRole).toString() == "game"_L1)
                out << model.data(model.index(row), GameRows::TitleRole).toString();
        return out;
    }

    static inline const QStringList Library{
        u"7 Days to Die"_s,
        u"ＡＢＣ Arcade"_s,
        u"Brütal Legend"_s,
        u"DARK SOULS™ III"_s,
        u"DARK SOULS™: REMASTERED"_s,
        u"Half-Life 2"_s,
        u"S.T.A.L.K.E.R.: Shadow of Chernobyl"_s,
        u"あつまれ どうぶつの森"_s,
        u"ペルソナ３ リロード"_s,
        u"大神 絶景版"_s,
    };

private slots:
    void dividersAndLetters()
    {
        GameRows model;
        QCOMPARE(rows(model), QStringList{u"search  "_s});
        model.setGames(games({u"7 Days to Die"_s, u"Alan Wake"_s, u"Alien: Isolation"_s, u"DOOM"_s,
                              u"あつまれ どうぶつの森"_s, u"ペルソナ３ リロード"_s, u"大神 絶景版"_s}));
        QCOMPARE(rows(model), (QStringList{u"search  "_s, u"divider # #"_s, u"game # 7 Days to Die"_s, u"divider a a"_s,
                                           u"game a Alan Wake"_s, u"game a Alien: Isolation"_s, u"divider d d"_s,
                                           u"game d DOOM"_s, u"divider あ あ"_s, u"game あ あつまれ どうぶつの森"_s,
                                           u"divider は は"_s, u"game は ペルソナ３ リロード"_s, u"divider 漢 漢"_s,
                                           u"game 漢 大神 絶景版"_s}));
        QCOMPARE(model.letters(), (QStringList{u"#"_s, u"a"_s, u"d"_s, u"あ"_s, u"は"_s, u"漢"_s}));
        QCOMPARE(model.data(model.index(4), GameRows::AppidRole).toString(), u"101"_s);
        QCOMPARE(model.data(model.index(0), GameRows::AppidRole).toString(), QString());
        QList<QByteArray> names = model.roleNames().values();
        std::sort(names.begin(), names.end());
        QCOMPARE(names, (QList<QByteArray>{"appid", "kind", "letter", "title"}));
    }

    void rowOfLetter()
    {
        GameRows model;
        model.setGames(games(Library));
        QCOMPARE(model.rowOfLetter(u"#"_s), 1);
        QCOMPARE(model.rowOfLetter(u"b"_s), 5);
        QCOMPARE(model.rowOfLetter(u"d"_s), 7);
        QCOMPARE(model.rowOfLetter(u"z"_s), -1);
        QCOMPARE(model.data(model.index(model.rowOfLetter(u"漢"_s)), GameRows::KindRole).toString(), u"divider"_s);
        model.setQuery(u"souls"_s);
        QCOMPARE(model.rowOfLetter(u"d"_s), 1);
        QCOMPARE(model.rowOfLetter(u"#"_s), -1);
    }

    void count()
    {
        GameRows model;
        QSignalSpy changed(&model, &GameRows::countChanged);
        QCOMPARE(model.count(), 0);
        model.setGames(games(Library));
        QCOMPARE(model.count(), 10);
        QCOMPARE(changed.count(), 1);
        model.setQuery(u"dark"_s);
        QCOMPARE(model.count(), 2);
        QCOMPARE(changed.count(), 2);
        model.setQuery(u"DARK"_s);
        QCOMPARE(changed.count(), 2);
        model.setQuery(u"nothing like it"_s);
        QCOMPARE(model.count(), 0);
        QCOMPARE(rows(model), QStringList{u"search  nothing like it"_s});
    }

    void fold_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("folded");
        QTest::newRow("trade mark") << u"DARK SOULS™ III"_s << u"dark souls iii"_s;
        QTest::newRow("accent") << u"Brütal Legend"_s << u"brutal legend"_s;
        QTest::newRow("dots") << u"S.T.A.L.K.E.R.: Shadow of Chernobyl"_s << u"stalker shadow of chernobyl"_s;
        QTest::newRow("hyphen") << u"Half-Life 2"_s << u"halflife 2"_s;
        QTest::newRow("full width") << u"ＡＢＣ Arcade"_s << u"abc arcade"_s;
        QTest::newRow("spaces") << u"  Tom Clancy’s   The Division ® "_s << u"tom clancys the division"_s;
        QTest::newRow("kana") << u"ペルソナ３ リロード"_s << u"ヘルソナ3 リロート"_s;
    }

    void fold()
    {
        QFETCH(QString, title);
        QFETCH(QString, folded);
        QCOMPARE(GameRows::fold(title), folded);
    }

    void query_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("found");
        QTest::newRow("souls iii") << u"souls iii"_s << QStringList{u"DARK SOULS™ III"_s};
        QTest::newRow("words apart") << u"dark iii"_s << QStringList{u"DARK SOULS™ III"_s};
        QTest::newRow("both souls") << u"Souls"_s << QStringList{u"DARK SOULS™ III"_s, u"DARK SOULS™: REMASTERED"_s};
        QTest::newRow("no tm") << u"souls tm"_s << QStringList();
        QTest::newRow("brutal") << u"brutal"_s << QStringList{u"Brütal Legend"_s};
        QTest::newRow("brütal") << u"BRÜTAL"_s << QStringList{u"Brütal Legend"_s};
        QTest::newRow("stalker") << u"stalker"_s << QStringList{u"S.T.A.L.K.E.R.: Shadow of Chernobyl"_s};
        QTest::newRow("halflife") << u"halflife"_s << QStringList{u"Half-Life 2"_s};
        QTest::newRow("half life") << u"half life"_s << QStringList{u"Half-Life 2"_s};
        QTest::newRow("full width") << u"abc"_s << QStringList{u"ＡＢＣ Arcade"_s};
        QTest::newRow("latin over kana") << u"persona"_s << QStringList();
        QTest::newRow("no match") << u"souls iv"_s << QStringList();
        QTest::newRow("blank") << u"   "_s << Library;
    }

    void query()
    {
        QFETCH(QString, query);
        QFETCH(QStringList, found);
        GameRows model;
        model.setGames(games(Library));
        model.setQuery(query);
        QCOMPARE(titles(model), found);
        QCOMPARE(model.count(), found.size());
        QCOMPARE(model.data(model.index(0), GameRows::TitleRole).toString(), query);
        QStringList letters;
        for (int row = 1; row < model.rowCount(); ++row)
            if (model.data(model.index(row), GameRows::KindRole).toString() == "divider"_L1)
                letters << model.data(model.index(row), GameRows::LetterRole).toString();
        QCOMPARE(model.letters(), letters);
        for (int row = 1; row < model.rowCount(); ++row) {
            const QString kind = model.data(model.index(row), GameRows::KindRole).toString();
            const QString letter = model.data(model.index(row), GameRows::LetterRole).toString();
            QVERIFY(kind != "divider"_L1
                    || model.data(model.index(row + 1), GameRows::LetterRole).toString() == letter);
        }
    }

    void kanaSectioned()
    {
        GameRows model;
        model.setGames(games(Library));
        QCOMPARE(model.data(model.index(model.rowOfLetter(u"あ"_s) + 1), GameRows::TitleRole).toString(),
                 u"あつまれ どうぶつの森"_s);
        QCOMPARE(model.data(model.index(model.rowOfLetter(u"は"_s) + 1), GameRows::TitleRole).toString(),
                 u"ペルソナ３ リロード"_s);
        model.setQuery(u"rirodo"_s);
        QCOMPARE(model.count(), 0);
    }

    void resets()
    {
        GameRows model;
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.setGames(games(Library));
        QCOMPARE(reset.count(), 1);
        model.setQuery(u"dark"_s);
        QCOMPARE(reset.count(), 2);
        model.letters();
        model.rowOfLetter(u"d"_s);
        model.count();
        model.data(model.index(1), GameRows::TitleRole);
        model.rowCount();
        QCOMPARE(reset.count(), 2);
        QCOMPARE(inserted.count() + removed.count() + changed.count(), 0);
    }
};

QTEST_GUILESS_MAIN(GameRowsTest)
#include "tst_gamerows.moc"
