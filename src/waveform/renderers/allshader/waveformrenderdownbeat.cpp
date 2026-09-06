#include "waveform/renderers/allshader/waveformrenderdownbeat.h"

#include <QDomNode>

#include "moc_waveformrenderdownbeat.cpp"
#include "rendergraph/geometry.h"
#include "rendergraph/material/unicolormaterial.h"
#include "rendergraph/vertexupdaters/vertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/downbeat.h"
#include "track/track.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

using namespace rendergraph;

namespace {
/// Width of a downbeat line in logical pixels. A regular beat line is 1 px, so
/// the downbeat stays recognizable as part of the same grid but reads as the
/// bar start at a glance.
constexpr float kDownbeatLineWidth = 3.f;

/// Used when the skin does not provide a <DownbeatColor>. Matches the colour of
/// the bar indicators in the toolbar so both read as the same feature.
const QColor kDefaultDownbeatColor = QColor(0x4d, 0xd2, 0xff);
} // namespace

namespace allshader {

WaveformRenderDownbeat::WaveformRenderDownbeat(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : ::WaveformRendererAbstract(waveformWidget),
          m_color(kDefaultDownbeatColor),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
    initForRectangles<UniColorMaterial>(0);
    setUsePreprocess(true);
}

void WaveformRenderDownbeat::setup(const QDomNode& node, const SkinContext& skinContext) {
    const QColor color =
            QColor(skinContext.selectString(node, QStringLiteral("DownbeatColor")));
    m_color = color.isValid() ? WSkinColor::getCorrectColor(color).toRgb()
                              : kDefaultDownbeatColor;
}

void WaveformRenderDownbeat::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderDownbeat::preprocess() {
    if (!preprocessInner()) {
        geometry().allocate(0);
        markDirtyGeometry();
    }
}

bool WaveformRenderDownbeat::preprocessInner() {
    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    const auto downbeatPosition = trackInfo->getDownbeatPosition();
    if (!downbeatPosition.isValid()) {
        // No downbeat marked: draw nothing, the grid looks like it always did.
        return false;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (!trackBeats) {
        return false;
    }

    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return false;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    m_color.setAlphaF(alpha / 100.0f);

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0.0) {
        return false;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);

    if (!startPosition.isValid() || !endPosition.isValid()) {
        return false;
    }

    const float rendererBreadth = m_waveformRenderer->getBreadth();

    const int numVerticesPerLine = 6; // 2 triangles

    // The bar phase is only computed once for the first visible beat; from
    // there it just cycles, which keeps this as cheap as the beat renderer.
    int phase = mixxx::kInvalidDownbeatPhase;
    int numDownbeatsInRange = 0;
    {
        auto it = trackBeats->iteratorFrom(startPosition);
        if (it != trackBeats->cend() && *it <= endPosition) {
            phase = mixxx::downbeatPhaseAt(*trackBeats, downbeatPosition, *it);
        }
        if (phase == mixxx::kInvalidDownbeatPhase) {
            return false;
        }
        for (int p = phase; it != trackBeats->cend() && *it <= endPosition; ++it) {
            if (p == 0) {
                numDownbeatsInRange++;
            }
            p = (p + 1) % mixxx::kBeatsPerBar;
        }
    }

    if (numDownbeatsInRange == 0) {
        return false;
    }

    const int reserved = numDownbeatsInRange * numVerticesPerLine;
    geometry().allocate(reserved);

    VertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::Point2D>()};

    for (auto it = trackBeats->iteratorFrom(startPosition);
            it != trackBeats->cend() && *it <= endPosition;
            ++it) {
        if (phase == 0) {
            double beatPosition = it->toEngineSamplePos();
            double xBeatPoint =
                    m_waveformRenderer->transformSamplePositionInRendererWorld(
                            beatPosition, positionType);

            xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;

            // Centre the wider line on the beat so it does not appear shifted
            // against the regular 1 px beat line underneath it.
            const float x1 = static_cast<float>(xBeatPoint) -
                    (kDownbeatLineWidth - 1.f) / 2.f;
            const float x2 = x1 + kDownbeatLineWidth;

            vertexUpdater.addRectangle({x1, 0.f},
                    {x2, m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth});
        }
        phase = (phase + 1) % mixxx::kBeatsPerBar;
    }
    markDirtyGeometry();

    DEBUG_ASSERT(reserved == vertexUpdater.index());

    material().setUniform(1, m_color);
    markDirtyMaterial();

    return true;
}

} // namespace allshader
