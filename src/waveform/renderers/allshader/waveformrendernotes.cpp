#include "waveform/renderers/allshader/waveformrendernotes.h"

#include <QDomNode>
#include <QFontMetricsF>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#include "control/controlproxy.h"
#include "moc_waveformrendernotes.cpp"
#include "rendergraph/context.h"
#include "rendergraph/geometry.h"
#include "rendergraph/geometrynode.h"
#include "rendergraph/material/texturematerial.h"
#include "rendergraph/material/unicolormaterial.h"
#include "rendergraph/texture.h"
#include "rendergraph/vertexupdaters/texturedvertexupdater.h"
#include "rendergraph/vertexupdaters/vertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/note.h"
#include "track/track.h"
#include "util/assert.h"
#include "util/roundtopixel.h"
#include "waveform/renderers/allshader/digitsrenderer.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "waveform/waveformwidgetfactory.h"
#include "widget/wskincolor.h"

using namespace rendergraph;

// On the use of QPainter: like the other allshader renderers, we only use
// QPainter to draw a note's label onto a QImage, which is then uploaded as a
// texture and drawn with a GLSL shader. This happens only when the note content
// changes, not every frame. See the comment in waveformrendermark.cpp.

namespace allshader {

// A single note label: a textured rectangle whose texture is the baked label
// image. Modeled on WaveformMarkNode in waveformrendermark.cpp.
class NoteLabelNode : public rendergraph::GeometryNode {
  public:
    NoteLabelNode(rendergraph::Context* pContext, const QImage& image) {
        initForRectangles<TextureMaterial>(1);
        updateTexture(pContext, image);
    }
    void updateTexture(rendergraph::Context* pContext, const QImage& image) {
        dynamic_cast<TextureMaterial&>(material())
                .setTexture(std::make_unique<Texture>(pContext, image));
        m_textureWidth = static_cast<float>(image.width());
        m_textureHeight = static_cast<float>(image.height());
    }
    void setQuad(float x, float y, float devicePixelRatio) {
        TexturedVertexUpdater vertexUpdater{
                geometry().vertexDataAs<Geometry::TexturedPoint2D>()};
        vertexUpdater.addRectangle({x, y},
                {x + m_textureWidth / devicePixelRatio,
                        y + m_textureHeight / devicePixelRatio},
                {0.f, 0.f},
                {1.f, 1.f});
    }
    void hideQuad() {
        TexturedVertexUpdater vertexUpdater{
                geometry().vertexDataAs<Geometry::TexturedPoint2D>()};
        vertexUpdater.addRectangle({0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f});
    }
    float textureWidth() const {
        return m_textureWidth;
    }
    float textureHeight() const {
        return m_textureHeight;
    }

  private:
    float m_textureWidth{};
    float m_textureHeight{};
};

namespace {

QColor contrastingTextColor(const QColor& background) {
    // Rec. 601 luma: pick black text on a light background, white on a dark one.
    const double luma = 0.299 * background.redF() +
            0.587 * background.greenF() + 0.114 * background.blueF();
    return luma > 0.5 ? QColor(Qt::black) : QColor(Qt::white);
}

// Format a duration as "m:ss.cc" for the ETA countdown (copied from
// waveformrendermark.cpp, where it formats the until-mark time).
QString timeSecToString(double timeSec) {
    int hundredths = std::lround(timeSec * 100.0);
    int seconds = hundredths / 100;
    hundredths -= seconds * 100;
    int minutes = seconds / 60;
    seconds -= minutes * 60;

    return QString::asprintf("%d:%02d.%02d", minutes, seconds, hundredths);
}

} // namespace

} // namespace allshader

allshader::WaveformRenderNotes::WaveformRenderNotes(
        WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : ::WaveformRendererAbstract(waveformWidget),
          // Distinct default until the per-deck note color setting (concept
          // section 9, a later phase) is wired into the preferences.
          m_color(255, 200, 0) {
    Q_UNUSED(type); // notes are always drawn at the play position for now

    {
        auto pNode = std::make_unique<GeometryNode>();
        m_pLinesNode = pNode.get();
        m_pLinesNode->initForRectangles<UniColorMaterial>(0);
        appendChildNode(std::move(pNode));
    }
    {
        auto pNode = std::make_unique<Node>();
        m_pLabelNodesParent = pNode.get();
        appendChildNode(std::move(pNode));
    }
    {
        // Drawn last (on top) -- the live-ETA countdown digits at the play marker.
        auto pNode = std::make_unique<DigitsRenderNode>();
        m_pDigitsNode = pNode.get();
        appendChildNode(std::move(pNode));
    }
}

void allshader::WaveformRenderNotes::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

bool allshader::WaveformRenderNotes::init() {
    // Same controls the until-mark countdown uses (see waveformrendermark.cpp):
    // the play state selects the display mode, time_remaining drives the time.
    m_pPlayControl = std::make_unique<ControlProxy>(
            m_waveformRenderer->getGroup(), QStringLiteral("play"));
    m_pTimeRemainingControl = std::make_unique<ControlProxy>(
            m_waveformRenderer->getGroup(), QStringLiteral("time_remaining"));
    return true;
}

void allshader::WaveformRenderNotes::setup(
        const QDomNode& node, const SkinContext& skinContext) {
    const QString colorName =
            skinContext.selectString(node, QStringLiteral("NoteColor"));
    if (!colorName.isEmpty()) {
        m_color = WSkinColor::getCorrectColor(QColor(colorName)).toRgb();
    }
}

QImage allshader::WaveformRenderNotes::bakeLabel(
        const QString& content, float devicePixelRatio) const {
    QString text = content.simplified(); // single line for now; markdown later
    if (text.isEmpty()) {
        text = QStringLiteral("(empty)"); // placeholder for an empty note
    }

    QFont font;
    font.setPointSizeF(9.0);
    const QFontMetricsF metrics{font};

    constexpr float kPaddingX = 4.f;
    constexpr float kPaddingY = 2.f;
    const float w = std::ceil(
            static_cast<float>(metrics.horizontalAdvance(text)) + 2.f * kPaddingX);
    const float h = std::ceil(
            static_cast<float>(metrics.height()) + 2.f * kPaddingY);

    QImage image(static_cast<int>(std::lround(w * devicePixelRatio)),
            static_cast<int>(std::lround(h * devicePixelRatio)),
            QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);

    QColor background = m_color;
    background.setAlphaF(0.85f);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), 3.0, 3.0);
    painter.setFont(font);
    painter.setPen(contrastingTextColor(background));
    painter.drawText(
            QRectF(kPaddingX, kPaddingY, w - 2.f * kPaddingX, h - 2.f * kPaddingY),
            Qt::AlignLeft | Qt::AlignVCenter,
            text);
    painter.end();

    return image;
}

void allshader::WaveformRenderNotes::rebuildLabels(
        const QList<NotePointer>& notes, float devicePixelRatio) {
    // Detach (and thereby destroy) the existing label nodes. This is safe here
    // because update() runs with a current OpenGL context.
    while (auto* pChild = m_pLabelNodesParent->firstChild()) {
        m_pLabelNodesParent->detachChildNode(pChild);
    }
    m_labelNodes.clear();
    m_labelNodes.reserve(notes.size());

    auto* pContext = m_waveformRenderer->getContext();
    for (const auto& pNote : notes) {
        auto pNode = std::make_unique<NoteLabelNode>(
                pContext, bakeLabel(pNote->getContent(), devicePixelRatio));
        m_labelNodes.push_back(pNode.get());
        m_pLabelNodesParent->appendChildNode(std::move(pNode));
    }
}

void allshader::WaveformRenderNotes::update() {
    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();
    const double trackSamples = m_waveformRenderer->getTrackSamples();
    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();
    const float breadth = m_waveformRenderer->getBreadth();

    const QList<NotePointer> notes = (trackInfo && trackSamples > 0.0)
            ? trackInfo->getNotes()
            : QList<NotePointer>{};

    const auto roundToPixel = createFunctionRoundToPixel(devicePixelRatio);

    // --- marker lines: one vertical rectangle per note (like WaveformRenderBeat)
    {
        constexpr int numVerticesPerLine = 6; // 2 triangles
        m_pLinesNode->geometry().allocate(
                static_cast<int>(notes.size()) * numVerticesPerLine);
        VertexUpdater vertexUpdater{
                m_pLinesNode->geometry().vertexDataAs<Geometry::Point2D>()};
        for (const auto& pNote : notes) {
            const mixxx::audio::FramePos position = pNote->getPosition();
            if (!position.isValid()) {
                vertexUpdater.addRectangle({0.f, 0.f}, {0.f, 0.f});
                continue;
            }
            const float x = roundToPixel(static_cast<float>(
                    m_waveformRenderer->transformSamplePositionInRendererWorld(
                            position.toEngineSamplePos(),
                            ::WaveformRendererAbstract::Play)));
            vertexUpdater.addRectangle({x, 0.f}, {x + 1.f, breadth});
        }
        m_pLinesNode->markDirtyGeometry();
        m_pLinesNode->material().setUniform(1, m_color);
        m_pLinesNode->markDirtyMaterial();
    }

    // --- labels: re-bake textures only when the content set or metrics change
    QStringList contents;
    contents.reserve(notes.size());
    for (const auto& pNote : notes) {
        contents.append(pNote->getContent());
    }
    if (contents != m_cachedContents ||
            devicePixelRatio != m_cachedDevicePixelRatio ||
            breadth != m_cachedBreadth) {
        rebuildLabels(notes, devicePixelRatio);
        m_cachedContents = contents;
        m_cachedDevicePixelRatio = devicePixelRatio;
        m_cachedBreadth = breadth;
    }

    DEBUG_ASSERT(m_labelNodes.size() == static_cast<size_t>(notes.size()));
    const int labelCount =
            std::min(static_cast<int>(notes.size()), static_cast<int>(m_labelNodes.size()));

    // --- find the next upcoming note (smallest position past the play marker)
    const double playPosition =
            m_waveformRenderer->getTruePosSample(::WaveformRendererAbstract::Play);
    int nextNoteIndex = -1;
    double nextNotePosition = std::numeric_limits<double>::max();
    for (int i = 0; i < labelCount; ++i) {
        const mixxx::audio::FramePos position = notes[i]->getPosition();
        if (!position.isValid()) {
            continue;
        }
        const double samplePosition = position.toEngineSamplePos();
        if (samplePosition >= playPosition + 1.0 && samplePosition < nextNotePosition) {
            nextNotePosition = samplePosition;
            nextNoteIndex = i;
        }
    }

    const bool playing = m_pPlayControl && m_pPlayControl->get() != 0.0;

    // --- standing view (concept section 6): labels anchored at their timecode
    if (!playing) {
        for (int i = 0; i < labelCount; ++i) {
            const mixxx::audio::FramePos position = notes[i]->getPosition();
            if (!position.isValid()) {
                m_labelNodes[i]->hideQuad();
                continue;
            }
            const float x = roundToPixel(static_cast<float>(
                    m_waveformRenderer->transformSamplePositionInRendererWorld(
                            position.toEngineSamplePos(),
                            ::WaveformRendererAbstract::Play)) +
                    2.f);
            m_labelNodes[i]->setQuad(x, 0.f, devicePixelRatio);
        }
        m_pDigitsNode->clear();
        return;
    }

    // --- live-ETA view (concept section 7): a countdown + the next note's
    // content, anchored at the play marker. All other labels are hidden.
    for (int i = 0; i < labelCount; ++i) {
        m_labelNodes[i]->hideQuad();
    }
    if (nextNoteIndex < 0) {
        m_pDigitsNode->clear();
        return;
    }

    updateUntilNote(playPosition, nextNotePosition);

    auto* pWaveformWidgetFactory = WaveformWidgetFactory::instance();
    const float maxHeightForText =
            std::roundf(breadth * pWaveformWidgetFactory->getUntilMarkTextHeightLimit());
    m_pDigitsNode->updateTexture(m_waveformRenderer->getContext(),
            static_cast<float>(pWaveformWidgetFactory->getUntilMarkTextPointSize()),
            maxHeightForText,
            devicePixelRatio);

    const QString beatsStr =
            m_etaShowBeats ? QString::number(m_beatsUntilNote) : QString{};
    const QString timeStr =
            m_etaShowTime ? timeSecToString(m_timeUntilNote) : QString{};

    const float ch = m_pDigitsNode->height();
    const bool multiLine =
            m_etaShowBeats && m_etaShowTime && ch * 2.f < maxHeightForText;

    const float digitsWidth = m_pDigitsNode->measure(beatsStr, timeStr, multiLine);

    NoteLabelNode* pLabel = m_labelNodes[nextNoteIndex];
    const float contentWidth = pLabel->textureWidth() / devicePixelRatio;
    const float contentHeight = pLabel->textureHeight() / devicePixelRatio;

    constexpr float kGap = 6.f; // between the countdown and the content
    const float gap = (digitsWidth > 0.f && contentWidth > 0.f) ? kGap : 0.f;
    const float totalWidth = digitsWidth + gap + contentWidth;

    const float playMarkerPos = static_cast<float>(
            m_waveformRenderer->getPlayMarkerPosition() *
            m_waveformRenderer->getLength());
    const float blockLeft = roundToPixel(m_etaAlignRightEdgeAtPlayhead
                    ? playMarkerPos - totalWidth
                    : playMarkerPos);

    // Digits vertically centered; for a two-line block the first line sits one
    // line-height above the center (the second is drawn below it by update()).
    const float digitsY = roundToPixel(
            multiLine ? breadth / 2.f - ch : breadth / 2.f - ch / 2.f);
    m_pDigitsNode->update(blockLeft, digitsY, multiLine, beatsStr, timeStr);

    const float contentX = roundToPixel(blockLeft + digitsWidth + gap);
    const float contentY = roundToPixel(breadth / 2.f - contentHeight / 2.f);
    pLabel->setQuad(contentX, contentY, devicePixelRatio);
}

void allshader::WaveformRenderNotes::updateUntilNote(
        double playPosition, double nextNotePosition) {
    m_beatsUntilNote = 0;
    m_timeUntilNote = 0.0;
    if (nextNotePosition == std::numeric_limits<double>::max()) {
        return;
    }

    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();
    if (!trackInfo) {
        return;
    }

    const double endPosition = m_waveformRenderer->getTrackSamples();
    const double remainingTime =
            m_pTimeRemainingControl ? m_pTimeRemainingControl->get() : 0.0;

    // Beats until the note (same iterator logic as
    // WaveformRenderMark::updateUntilMark).
    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (trackBeats) {
        auto itA = trackBeats->iteratorFrom(
                mixxx::audio::FramePos::fromEngineSamplePos(playPosition));
        auto itB = trackBeats->iteratorFrom(
                mixxx::audio::FramePos::fromEngineSamplePos(nextNotePosition));

        // itB is the beat at or after nextNotePosition; pick the closer of it
        // and the previous beat.
        if (itB->toEngineSamplePos() > nextNotePosition) {
            if (nextNotePosition - (itB - 1)->toEngineSamplePos() <
                    itB->toEngineSamplePos() - nextNotePosition) {
                itB--;
            }
        }

        if (std::abs(itA->toEngineSamplePos() - playPosition) < 1) {
            m_beatsUntilNote = static_cast<int>(std::distance(itA, itB));
        } else {
            itA--;
            m_beatsUntilNote = static_cast<int>(std::distance(itA, itB));
        }
    }

    // As endPosition - playPosition corresponds with remainingTime, take the
    // proportional part up to nextNotePosition.
    if (endPosition > playPosition) {
        m_timeUntilNote = std::max(0.0,
                remainingTime * (nextNotePosition - playPosition) /
                        (endPosition - playPosition));
    }
}
