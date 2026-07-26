#pragma once

#include <rubberband/RubberBandStretcher.h>

#include <QRunnable>
#include <QSemaphore>
#include <array>
#include <atomic>
#include <memory>

#include "engine/bufferscalers/enginebufferscale.h"
#include "engine/bufferscalers/rubberbandwrapper.h"
#include "util/samplebuffer.h"

class ReadAheadManager;

// Uses librubberband to scale audio.  This class is not thread safe.
class EngineBufferScaleRubberBand final : public EngineBufferScale {
    Q_OBJECT
  public:
    explicit EngineBufferScaleRubberBand(
            ReadAheadManager* pReadAheadManager);
    /// Waits for a prime deferred by clearAsync() that may still run on a
    /// worker thread before the object goes away.
    ~EngineBufferScaleRubberBand() override;

    EngineBufferScaleRubberBand(const EngineBufferScaleRubberBand&) = delete;
    EngineBufferScaleRubberBand& operator=(const EngineBufferScaleRubberBand&) = delete;

    EngineBufferScaleRubberBand(EngineBufferScaleRubberBand&&) = delete;
    EngineBufferScaleRubberBand& operator=(EngineBufferScaleRubberBand&&) = delete;

    // Let EngineBuffer know if engine v3 is available
    static bool isEngineFinerAvailable();

    // Enable engine v3 if available
    void useEngineFiner(bool enable);

    void setScaleParameters(double base_rate,
                            double* pTempoRatio,
                            double* pPitchRatio) override;

    double scaleBuffer(
            CSAMPLE* pOutputBuffer,
            SINT iOutputBufferSize) override;

    // Flush buffer.
    void clear() override;
    // Like clear(), but defers the expensive stretcher re-priming (reset plus
    // feeding the silence start pad, ~10ms for a STEM deck) to a worker
    // thread. Called by the engine when a deck stops rolling, so neither the
    // stop nor the next play start pays for the re-priming inside the
    // real-time callback. The next access to the stretcher synchronizes with
    // the deferred work via ensurePrimed().
    void clearAsync() override;

  private:
    // Reset RubberBand library with new audio signal
    void onSignalChanged() override;

    /// Calls `m_pRubberBand->getPreferredStartPad()`, with backwards
    /// compatibility for older librubberband versions.
    size_t getPreferredStartPad() const;
    /// Calls `m_pRubberBand->getStartDelay()`, with backwards compatibility for
    /// older librubberband versions.
    size_t getStartDelay() const;
    int runningEngineVersion();
    /// Reset the rubberband instance and run the prerequisite amount of padding
    /// through it. This should be used instead of calling
    /// `m_pRubberBand->reset()` directly.
    void reset();

    void deinterleaveAndProcess(const CSAMPLE* pBuffer, SINT frames);
    SINT retrieveAndDeinterleave(CSAMPLE* pBuffer, SINT frames);

    // The read-ahead manager that we use to fetch samples
    ReadAheadManager* m_pReadAheadManager;

    RubberBandWrapper m_rubberBand;

    /// The audio buffers samples used to send audio to Rubber Band and to
    /// receive processed audio from Rubber Band. This is needed because Mixxx
    /// uses interleaved buffers in most other places.
    std::vector<mixxx::SampleBuffer> m_buffers;
    /// These point to the buffers in `m_buffers`. They can be defined here
    /// since this object cannot be moved or copied.
    std::vector<float*> m_bufferPtrs;

    /// Contains interleaved samples read from `m_pReadAheadManager`. These need
    /// to be deinterleaved before they can be passed to Rubber Band.
    mixxx::SampleBuffer m_interleavedReadBuffer;

    // Holds the playback direction
    bool m_bBackwards;
    /// The amount of silence padding that still needs to be dropped from the
    /// retrieve samples in `retrieveAndDeinterleave()`. See the `reset()`
    /// function for an explanation.
    SINT m_remainingPaddingInOutput = 0;

    bool m_useEngineFiner;

    /// State of the deferred priming started by clearAsync().
    enum class PrimeState : int {
        Primed,   ///< the stretcher is ready to be used by the engine thread
        InFlight, ///< a worker thread owns the stretcher and runs reset()
    };

    /// Trampoline that lets a QThreadPool worker run runDeferredPrime().
    /// Reused for every prime; never auto-deleted.
    class PrimeTask : public QRunnable {
      public:
        explicit PrimeTask(EngineBufferScaleRubberBand* pScaler)
                : m_pScaler(pScaler) {
            setAutoDelete(false);
        }
        void run() override {
            m_pScaler->runDeferredPrime();
        }

      private:
        EngineBufferScaleRubberBand* const m_pScaler;
    };

    void runDeferredPrime();
    /// Synchronize with a prime deferred by clearAsync(). Must be called
    /// before any other access to m_rubberBand or the scratch buffers. All
    /// callers run on the engine thread, or on a thread that holds the
    /// engine's pause lock - never concurrently.
    void ensurePrimed();

    std::atomic<PrimeState> m_primeState;
    QSemaphore m_primeDone;
    PrimeTask m_primeTask;
};
