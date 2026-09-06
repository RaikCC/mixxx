#pragma once

#include <memory>

#include "audio/frame.h"
#include "control/controlvalue.h"
#include "engine/controls/enginecontrol.h"
#include "preferences/usersettings.h"
#include "track/beats.h"
#include "track/track_decl.h"

class ControlProxy;
class ControlObject;
class ControlPushButton;

class ClockControl: public EngineControl {
    Q_OBJECT
  public:
    ClockControl(const QString& group,
            UserSettingsPointer pConfig);

    ~ClockControl() override;

    void updateIndicators(const double dRate,
            mixxx::audio::FramePos currentPosition,
            mixxx::audio::SampleRate sampleRate);

    void trackLoaded(TrackPointer pNewTrack) override;
    void trackBeatsUpdated(mixxx::BeatsPointer pBeats) override;

    /// Called by EngineBuffer whenever the loaded track's downbeat changes.
    void trackDownbeatUpdated(mixxx::audio::FramePos downbeatPosition);

  private slots:
    /// `downbeat_set`: makes the beat closest to the play position the first
    /// beat of a bar, or removes the track's downbeat again if that beat
    /// already is one.
    void slotDownbeatSet(double v);

  private:
    /// Publishes which beat of the bar is currently played via
    /// `downbeat_phase`. Cheap enough to run on every engine callback.
    void updateDownbeatIndicator(mixxx::audio::FramePos currentPosition);

    std::unique_ptr<ControlObject> m_pCOBeatActive;

    // Downbeat indicator (fork feature, see src/track/downbeat.h)
    std::unique_ptr<ControlPushButton> m_pCODownbeatSet;
    /// 1 while the loaded track has a downbeat, 0 otherwise.
    std::unique_ptr<ControlObject> m_pCODownbeatActive;
    /// Beat of the bar currently played: 0..3, or -1 if unknown.
    std::unique_ptr<ControlObject> m_pCODownbeatPhase;
    /// Written from the GUI thread, read by the engine thread.
    ControlValueAtomic<mixxx::audio::FramePos> m_downbeatPosition;

    // ControlObjects that come from LoopingControl
    std::unique_ptr<ControlProxy> m_pLoopEnabled;
    std::unique_ptr<ControlProxy> m_pLoopStartPosition;
    std::unique_ptr<ControlProxy> m_pLoopEndPosition;

    // True is forward direction, False is reverse
    bool m_lastPlayDirectionWasForwards;

    mixxx::audio::FramePos m_lastEvaluatedPosition;
    mixxx::audio::FramePos m_prevBeatPosition;
    mixxx::audio::FramePos m_nextBeatPosition;
    mixxx::audio::FrameDiff_t m_blinkIntervalFrames;

    enum class StateMachine : int {
        afterBeatDirectionChanged =
                2, /// Direction changed to reverse playing while forward playing indication was on
        afterBeatActive =
                1, /// Forward playing, set at the beat and set back to 0.0 at 20% of beat distance
        outsideIndicationArea =
                0, /// Outside -20% ... +20% of the beat distance
        beforeBeatActive =
                -1, /// Reverse playing, set at the beat and set back to 0.0 at -20% of beat distance
        beforeBeatDirectionChanged =
                -2 /// Direction changed to forward playing while reverse playing indication was on
    };

    StateMachine m_internalState;

    // m_pBeats is written from an engine worker thread
    mixxx::BeatsPointer m_pBeats;
};
