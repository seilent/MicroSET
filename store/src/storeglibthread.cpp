#include "storeglibthread.h"

#include <QMutexLocker>

#pragma push_macro("signals")
#undef signals
#include <gio/gio.h>
#pragma pop_macro("signals")

namespace {

struct PostedWork
{
    std::function<void()> fn;
};

}

StoreGlibThread::StoreGlibThread(QObject *parent)
    : QThread(parent)
{
}

StoreGlibThread::~StoreGlibThread()
{
    stop();
}

void StoreGlibThread::startAndWait()
{
    QMutexLocker lock(&m_startMutex);
    m_started = false;
    {
        QMutexLocker postLock(&m_postMutex);
        m_context = g_main_context_new();
        m_stopping = false;
    }
    start();
    while (!m_started)
        m_startedWait.wait(&m_startMutex, 5000);
}

void StoreGlibThread::run()
{
    g_main_context_push_thread_default(m_context);
    bool stopping = false;
    {
        QMutexLocker lock(&m_postMutex);
        m_loop = g_main_loop_new(m_context, FALSE);
        stopping = m_stopping;
    }
    {
        QMutexLocker lock(&m_startMutex);
        m_started = true;
        m_startedWait.wakeAll();
    }
    if (!stopping)
        g_main_loop_run(m_loop);
    QMutexLocker lock(&m_postMutex);
    g_main_loop_unref(m_loop);
    m_loop = nullptr;
    g_main_context_pop_thread_default(m_context);
    g_main_context_unref(m_context);
    m_context = nullptr;
}

void StoreGlibThread::stop()
{
    if (!isRunning())
        return;
    {
        QMutexLocker lock(&m_postMutex);
        m_stopping = true;
        if (m_loop != nullptr)
            g_main_loop_quit(m_loop);
    }
    wait();
}

bool StoreGlibThread::post(std::function<void()> work)
{
    QMutexLocker lock(&m_postMutex);
    if (m_context == nullptr)
        return false;
    auto *posted = new PostedWork{std::move(work)};
    g_main_context_invoke_full(
        m_context, G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
            auto *entry = static_cast<PostedWork *>(data);
            entry->fn();
            return G_SOURCE_REMOVE;
        },
        posted, [](gpointer data) { delete static_cast<PostedWork *>(data); });
    return true;
}
