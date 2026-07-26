#include "engine/bufferscalers/rubberbandworkerpool.h"

#include <rubberband/RubberBandStretcher.h>

#include "engine/engine.h"
#include "util/assert.h"

RubberBandWorkerPool::RubberBandWorkerPool(UserSettingsPointer pConfig)
        : QThreadPool() {
    bool multiThreadedOnStereo = pConfig &&
            pConfig->getValue(ConfigKey(QStringLiteral("[App]"),
                                      QStringLiteral("keylock_multithreading")),
                    false);
    m_channelPerWorker = multiThreadedOnStereo
            ? mixxx::audio::ChannelCount::mono()
            : mixxx::audio::ChannelCount::stereo();
    DEBUG_ASSERT(mixxx::kMaxEngineChannelInputCount % m_channelPerWorker == 0);

    int numCore = QThread::idealThreadCount();
    int numRBTasksPerDeck = mixxx::kMaxEngineChannelInputCount / m_channelPerWorker;

    // With parallel deck processing (see EngineMixer::processChannels), two
    // STEM decks stretch concurrently. Each deck runs one of its tasks
    // inline in the thread that processes the deck, so it needs n-1 pool
    // workers - size the pool so two decks don't contend for workers, capped
    // to leave hardware threads for the engine callback and the deck
    // workers.
    int numWorkers = qBound(1, 2 * (numRBTasksPerDeck - 1), numCore - 2);

    qDebug() << "RubberBand will use" << numRBTasksPerDeck
             << "tasks per deck to scale the audio signal, with"
             << numWorkers << "pool workers";

    // QThreadPool names its threads after the pool's objectName. QThread also
    // applies it as the OS thread name, so the workers are identifiable in
    // ps/htop and RubberBandTask::run() can recognize them (see
    // kRubberBandWorkerThreadName).
    setObjectName(kRubberBandWorkerThreadName);
    setThreadPriority(QThread::HighPriority);
    setMaxThreadCount(numWorkers);

    // Note: no reserveThread() calls here! QThreadPool counts reserved slots
    // towards activeThreadCount(), so reserving maxThreadCount() slots up
    // front caps the pool at a single concurrently running worker (only the
    // recycled waiting thread passes the areAllThreadsActive() check in
    // QThreadPoolPrivate::tryStart()). All other stretch tasks then silently
    // fall back to running serially in the engine thread.
}
