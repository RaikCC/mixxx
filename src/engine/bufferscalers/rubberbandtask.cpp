#include "engine/bufferscalers/rubberbandtask.h"

#ifdef Q_OS_LINUX
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

#include <QThread>
#include <QtDebug>
#include <cstring>
#endif

#include "engine/bufferscalers/rubberbandworkerpool.h"
#include "engine/engine.h"
#include "util/assert.h"
#include "util/compatibility/qmutex.h"

#ifdef Q_OS_LINUX
namespace {

// The engine callback thread blocks in RubberBandWrapper::process() until all
// workers have finished their stretch task. On Linux the pool's
// QThread::HighPriority is a no-op (SCHED_OTHER has no static priorities), so
// an ordinary desktop workload may preempt a worker while the real-time
// callback waits for it - a priority inversion that surfaces as sporadic
// dropouts. Promote each pool worker to SCHED_FIFO once, just below the
// typical priority of the audio callback (PipeWire data-loop: 83).
constexpr int kWorkerRtPriority = 78;

void promoteWorkerToRtPriorityOnce() {
    thread_local bool s_attempted = false;
    if (s_attempted) {
        return;
    }
    s_attempted = true;

    // When the pool is exhausted, tasks run inline in the engine callback
    // thread (or, in tests, in arbitrary threads) - promote genuine pool
    // workers only.
    if (QThread::currentThread()->objectName() != kRubberBandWorkerThreadName) {
        return;
    }

    int prio = kWorkerRtPriority;
    struct rlimit limit;
    if (getrlimit(RLIMIT_RTPRIO, &limit) == 0 &&
            static_cast<int>(limit.rlim_cur) < prio) {
        prio = static_cast<int>(limit.rlim_cur);
    }
    if (prio <= 0) {
        qWarning() << "RubberBandTask: real-time scheduling not permitted"
                   << "(RLIMIT_RTPRIO is 0), keylock workers stay best-effort";
        return;
    }

    sched_param param{};
    param.sched_priority = prio;
    if (int err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param)) {
        qWarning() << "RubberBandTask: pthread_setschedparam failed:"
                   << strerror(err);
    } else {
        qDebug() << "RubberBandTask: worker promoted to SCHED_FIFO" << prio;
    }
}

} // namespace
#endif

RubberBandTask::RubberBandTask(
        size_t sampleRate, size_t channels, Options options)
        : RubberBand::RubberBandStretcher(sampleRate, channels, options),
          QRunnable(),
          m_completedSema(0),
          m_input(nullptr),
          m_samples(0),
          m_isFinal(false) {
    setAutoDelete(false);
}

void RubberBandTask::set(const float* const* input,
        size_t samples,
        bool isFinal) {
    DEBUG_ASSERT(m_completedSema.available() == 0);
    m_input = input;
    m_samples = samples;
    m_isFinal = isFinal;
}

void RubberBandTask::waitReady() {
    VERIFY_OR_DEBUG_ASSERT(m_input && m_samples) {
        return;
    };
    m_completedSema.acquire();
}

void RubberBandTask::run() {
#ifdef Q_OS_LINUX
    promoteWorkerToRtPriorityOnce();
#endif
    VERIFY_OR_DEBUG_ASSERT(m_completedSema.available() == 0 && m_input && m_samples) {
        return;
    };
    process(m_input,
            m_samples,
            m_isFinal);
    m_completedSema.release();
}
