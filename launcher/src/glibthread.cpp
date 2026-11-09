#include "glibthread.h"

#include <QMutexLocker>
#include <QSemaphore>

#pragma push_macro("signals")
#undef signals
#include <gio/gio.h>
#pragma pop_macro("signals")

namespace {

struct BlockingWork
{
    std::function<void()> fn;
    QSemaphore *done;
};

}

GlibThread::GlibThread(QObject *parent)
    : QThread(parent)
{
}

GlibThread::~GlibThread()
{
    stop();
}

void GlibThread::startAndWait()
{
    QMutexLocker lock(&m_startMutex);
    m_started = false;
    m_context = g_main_context_new();
    start();
    while (!m_started)
        m_startedWait.wait(&m_startMutex, 5000);
}

void GlibThread::run()
{
    g_main_context_push_thread_default(m_context);
    m_loop = g_main_loop_new(m_context, FALSE);
    {
        QMutexLocker lock(&m_startMutex);
        m_started = true;
        m_startedWait.wakeAll();
    }
    g_main_loop_run(m_loop);
    g_main_loop_unref(m_loop);
    m_loop = nullptr;
    g_main_context_pop_thread_default(m_context);
    g_main_context_unref(m_context);
    m_context = nullptr;
}

void GlibThread::stop()
{
    if (!isRunning())
        return;
    if (m_loop != nullptr)
        g_main_loop_quit(m_loop);
    wait(5000);
}

void GlibThread::runBlocking(std::function<void()> work)
{
    if (m_context == nullptr) {
        work();
        return;
    }
    QSemaphore done;
    auto *entry = new BlockingWork{std::move(work), &done};
    g_main_context_invoke_full(
        m_context, G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
            auto *entry = static_cast<BlockingWork *>(data);
            entry->fn();
            entry->done->release();
            return G_SOURCE_REMOVE;
        },
        entry, nullptr);
    done.acquire();
    delete entry;
}
