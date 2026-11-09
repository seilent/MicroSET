#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class DesktopHome : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroOs.DesktopHome1")
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY windowsChanged)
    Q_PROPERTY(uint transform READ transform NOTIFY transformChanged)

public:
    static QString service();
    static QString path();
    static QString interface();

    explicit DesktopHome(QObject *parent = nullptr);

    bool available() const;
    bool shown() const;
    QVariantList windows() const;
    uint transform() const;

    Q_INVOKABLE virtual void enter() = 0;
    Q_INVOKABLE virtual void toggle() = 0;
    Q_INVOKABLE virtual void leave() = 0;
    Q_INVOKABLE virtual void focusWindow(const QString &address) = 0;

signals:
    void availableChanged();
    void shownChanged();
    void windowsChanged();
    void transformChanged();
    void failed(const QString &method, const QString &message);

protected:
    void setAvailable(bool available);
    void setShown(bool shown);
    void setWindows(const QVariantList &windows);
    void setTransform(uint transform);

private:
    bool m_available = false;
    bool m_shown = false;
    QVariantList m_windows;
    uint m_transform = 0;
};
