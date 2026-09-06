#include "track/downbeat.h"

namespace {

/// Moves `it` to the beat at or before `position`. `Beats::iteratorFrom()`
/// returns the first beat *after* `position` unless `position` sits exactly on
/// a beat, so at most one step back is needed.
///
/// Returns false if the beat is not representable, which `Beats` signals by
/// returning its `cbegin()`/`cend()` sentinels.
bool beatIteratorAt(const mixxx::Beats& beats,
        mixxx::audio::FramePos position,
        mixxx::Beats::ConstIterator* pIt) {
    auto it = beats.iteratorFrom(position);
    if (it == beats.cbegin() || it == beats.cend()) {
        return false;
    }
    if (*it > position) {
        --it;
        if (it == beats.cbegin()) {
            return false;
        }
    }
    *pIt = it;
    return true;
}

} // namespace

namespace mixxx {

int downbeatPhaseAt(const Beats& beats,
        audio::FramePos downbeatPosition,
        audio::FramePos position) {
    if (!downbeatPosition.isValid() || !position.isValid()) {
        return kInvalidDownbeatPhase;
    }

    // The stored position is a plain frame position and may have drifted off
    // the grid, e.g. because the beatgrid was moved or re-analyzed after the
    // downbeat was set. Snap it back onto the grid before counting bars, so a
    // moved grid shifts the downbeat along instead of invalidating it.
    const auto anchorPosition = beats.findClosestBeat(downbeatPosition);
    if (!anchorPosition.isValid()) {
        return kInvalidDownbeatPhase;
    }

    Beats::ConstIterator anchorIt = beats.cend();
    Beats::ConstIterator it = beats.cend();
    if (!beatIteratorAt(beats, anchorPosition, &anchorIt) ||
            !beatIteratorAt(beats, position, &it)) {
        return kInvalidDownbeatPhase;
    }

    // Beat positions before the anchor give a negative difference, so the
    // remainder has to be lifted into 0..kBeatsPerBar-1 explicitly.
    const int beatsFromAnchor = it - anchorIt;
    return ((beatsFromAnchor % kBeatsPerBar) + kBeatsPerBar) % kBeatsPerBar;
}

bool isDownbeatAt(const Beats& beats,
        audio::FramePos downbeatPosition,
        audio::FramePos position) {
    return downbeatPhaseAt(beats, downbeatPosition, position) == 0;
}

} // namespace mixxx
