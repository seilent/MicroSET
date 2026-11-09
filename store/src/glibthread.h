#pragma once

#include <QMutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>

#include <functional>

struct _GMainContext;
struct _GMainLoop;

class GlibThread : public QThread
{
    Q_OBJECT

public:
    explicit GlibThread(QObject *parent = nullptr);
    ~GlibThread() override;

    void run() override;
    void startAndWait();
    void stop();
    bool post(std::function<void()> work);

private:
    _GMainContext *m_context = nullptr;
    _GMainLoop *m_loop = nullptr;
    QMutex m_postMutex;
    QMutex m_startMutex;
    QWaitCondition m_startedWait;
    bool m_started = false;
    bool m_stopping = false;
};
