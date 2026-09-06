#pragma once

#include "audio/frame.h"
#include "track/beats.h"

namespace mixxx {

/// Number of beats per bar assumed by the downbeat feature. Mixxx has no
/// time signature model, and everything this feature is used for is 4/4.
constexpr int kBeatsPerBar = 4;

/// Value of the `downbeat_phase` control (and the return value of
/// `downbeatPhaseAt()`) when no bar position can be determined, i.e. the track
/// has no downbeat, no beats, or the play position is unknown.
constexpr int kInvalidDownbeatPhase = -1;

/// Returns the position within the bar of the beat at or before `position`:
/// 0 for a downbeat, 1..3 for the remaining beats of the bar. Bars are counted
/// from the beat closest to `downbeatPosition`, in both directions.
///
/// Returns `kInvalidDownbeatPhase` if the phase cannot be determined.
int downbeatPhaseAt(const Beats& beats,
        audio::FramePos downbeatPosition,
        audio::FramePos position);

/// Returns whether the beat closest to `position` is a downbeat.
bool isDownbeatAt(const Beats& beats,
        audio::FramePos downbeatPosition,
        audio::FramePos position);

} // namespace mixxx
