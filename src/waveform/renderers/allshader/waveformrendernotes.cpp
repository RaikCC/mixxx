#include "waveform/renderers/allshader/waveformrendernotes.h"

#include <QDomNode>
#include <QFontMetricsF>
#include <QPainter>
#include <cmath>

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
#include "waveform/renderers/waveformwidgetrenderer.h"
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
}

void allshader::WaveformRenderNotes::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
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

    // --- position the label quads every frame, just to the right of the line
    DEBUG_ASSERT(m_labelNodes.size() == static_cast<size_t>(notes.size()));
    for (int i = 0; i < notes.size() && i < static_cast<int>(m_labelNodes.size());
            ++i) {
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
}
