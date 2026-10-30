#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace microdap {

class KeyboardModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString title READ title NOTIFY requestChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY requestChanged)
    Q_PROPERTY(QString unit READ unit NOTIFY requestChanged)
    Q_PROPERTY(bool number READ number NOTIFY requestChanged)
    Q_PROPERTY(QVariantList keys READ keys NOTIFY requestChanged)
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY textChanged)
    Q_PROPERTY(bool edited READ edited NOTIFY textChanged)
    Q_PROPERTY(int shift READ shift NOTIFY pageChanged)
    Q_PROPERTY(bool symbols READ symbols NOTIFY pageChanged)
    Q_PROPERTY(QVariantMap labels READ labels NOTIFY pageChanged)
    Q_PROPERTY(QVariantList chars READ chars NOTIFY pageChanged)

public:
    static constexpr int shiftDoubleMs = 350;

    explicit KeyboardModel(QObject *parent = nullptr);

    static QString charAt(bool symbols, int digit, const QString &dir);

    QString title() const;
    QString hint() const;
    QString unit() const;
    bool number() const;
    QVariantList keys() const;
    QString text() const;
    bool ready() const;
    bool edited() const;
    int shift() const;
    bool symbols() const;
    QVariantMap labels() const;
    QVariantList chars() const;

    Q_INVOKABLE void open(const QVariantMap &request);
    Q_INVOKABLE bool type(const QString &ch);
    Q_INVOKABLE bool erase();
    Q_INVOKABLE bool commit(int digit, const QString &dir);
    Q_INVOKABLE void tapShift();
    Q_INVOKABLE void stepShift();
    Q_INVOKABLE void toggleSymbols();
    Q_INVOKABLE QString refusal() const;

signals:
    void requestChanged();
    void textChanged();
    void pageChanged();

private:
    int bytes(const QString &s) const;
    QString label(const QString &code) const;
    QString pick(int digit, const QString &dir) const;
    void setText(const QString &text);

    QString m_title;
    QString m_hint;
    QString m_initial;
    QString m_unit;
    QString m_extras;
    bool m_number = false;
    int m_decimals = 0;
    int m_minBytes = 0;
    int m_maxBytes = 64;
    bool m_ranged = false;
    double m_minValue = 0;
    double m_maxValue = 0;
    QString m_text;
    int m_shift = 0;
    bool m_symbols = false;
    QElapsedTimer m_shiftAt;
};

}
