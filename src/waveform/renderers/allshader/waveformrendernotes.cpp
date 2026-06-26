#include "waveform/renderers/allshader/waveformrendernotes.h"

#include <QDomNode>

#include "moc_waveformrendernotes.cpp"
#include "rendergraph/geometry.h"
#include "rendergraph/material/unicolormaterial.h"
#include "rendergraph/vertexupdaters/vertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/note.h"
#include "track/track.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

using namespace rendergraph;

namespace allshader {

WaveformRenderNotes::WaveformRenderNotes(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : ::WaveformRendererAbstract(waveformWidget),
          // Distinct default until the per-deck note color setting (concept
          // section 9, a later phase) is wired into the preferences.
          m_color(255, 200, 0),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
    initForRectangles<UniColorMaterial>(0);
    setUsePreprocess(true);
}

void WaveformRenderNotes::setup(const QDomNode& node, const SkinContext& skinContext) {
    const QString colorName =
            skinContext.selectString(node, QStringLiteral("NoteColor"));
    if (!colorName.isEmpty()) {
        m_color = WSkinColor::getCorrectColor(QColor(colorName)).toRgb();
    }
}

void WaveformRenderNotes::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderNotes::preprocess() {
    if (!preprocessInner()) {
        geometry().allocate(0);
        markDirtyGeometry();
    }
}

bool WaveformRenderNotes::preprocessInner() {
    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    const auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                               : ::WaveformRendererAbstract::Play;

    const QList<NotePointer> notes = trackInfo->getNotes();
    if (notes.isEmpty()) {
        return false;
    }

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0.0) {
        return false;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const float rendererBreadth = m_waveformRenderer->getBreadth();

    const int numVerticesPerLine = 6; // 2 triangles

    const int reserved = static_cast<int>(notes.size()) * numVerticesPerLine;
    geometry().allocate(reserved);

    VertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::Point2D>()};

    for (const auto& pNote : notes) {
        const mixxx::audio::FramePos position = pNote->getPosition();
        if (!position.isValid()) {
            // Keep the vertex count in sync with the reservation above by
            // emitting a degenerate (zero-size) rectangle for invalid notes.
            vertexUpdater.addRectangle({0.f, 0.f}, {0.f, 0.f});
            continue;
        }
        double xPoint = m_waveformRenderer->transformSamplePositionInRendererWorld(
                position.toEngineSamplePos(), positionType);
        xPoint = qRound(xPoint * devicePixelRatio) / devicePixelRatio;

        const float x1 = static_cast<float>(xPoint);
        const float x2 = x1 + 1.f;

        vertexUpdater.addRectangle({x1, 0.f},
                {x2, m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth});
    }
    markDirtyGeometry();

    DEBUG_ASSERT(reserved == vertexUpdater.index());

    material().setUniform(1, m_color);
    markDirtyMaterial();

    return true;
}

} // namespace allshader
