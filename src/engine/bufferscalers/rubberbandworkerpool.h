#pragma once

#include <QThreadPool>

#include "audio/types.h"
#include "preferences/usersettings.h"
#include "util/singleton.h"

// Object (and OS) name of the pool's worker threads. RubberBandTask uses it
// to distinguish genuine pool workers from the engine callback thread, which
// runs tasks inline when the pool is exhausted.
inline constexpr QLatin1StringView kRubberBandWorkerThreadName("RBWorker");

// RubberBandWorkerPool is a global pool manager for RubberBandWorkerPool. It
// allows a the Engine thread to use a pool of agnostic RubberBandWorker which
// can be distributed stretching job
class RubberBandWorkerPool : public QThreadPool, public Singleton<RubberBandWorkerPool> {
  public:
    const mixxx::audio::ChannelCount& channelPerWorker() const {
        return m_channelPerWorker;
    }

  protected:
    RubberBandWorkerPool(UserSettingsPointer pConfig = nullptr);

  private:
    ;
    mixxx::audio::ChannelCount m_channelPerWorker;

    friend class Singleton<RubberBandWorkerPool>;
};
