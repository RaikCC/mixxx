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
    // Draws only the rightmost `visibleFraction` (0..1) of the texture, at the
    // matching sub-range of the quad whose left edge is x. Used by the proximity
    // indicator to reveal the contrast bar from the right edge leftward.
    void setQuadClippedLeft(float x, float y, float visibleFraction, float devicePixelRatio) {
        const float fullWidth = m_textureWidth / devicePixelRatio;
        const float uMin = 1.f - visibleFraction;
        TexturedVertexUpdater vertexUpdater{
                geometry().vertexDataAs<Geometry::TexturedPoint2D>()};
        vertexUpdater.addRectangle({x + fullWidth * uMin, y},
                {x + fullWidth, y + m_textureHeight / devicePixelRatio},
                {uMin, 0.f},
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

// Padding around the text inside the rounded note boxes. The note font size is
// a setting (concept section 9), threaded through these helpers as a parameter
// so labels, the live-ETA bar and the countdown digits stay one visual unit.
constexpr float kBoxPaddingX = 4.f;
constexpr float kBoxPaddingY = 2.f;

// Vertical layout of the stacked live-ETA bars: a small top margin, then a gap
// between bars. They stack downward and never run past the waveform bottom.
constexpr float kStackTopMargin = 2.f;
constexpr float kStackGap = 2.f;
// Horizontal padding inside the countdown field, and the side-by-side gap factor
// update() inserts between the beats and the time digits (= digit height * this).
constexpr float kFieldPadX = 6.f;
constexpr float kDigitsGapFactor = 0.75f;
// Gap between the countdown field and the note content inside an ETA bar.
constexpr float kEtaInnerGap = 6.f;

QFont etaFont(double pointSize) {
    QFont font;
    font.setPointSizeF(pointSize);
    return font;
}

// Logical height of a note box (label or countdown field) for the note font.
float etaBoxHeight(double pointSize) {
    const QFontMetricsF metrics{etaFont(pointSize)};
    return std::ceil(static_cast<float>(metrics.height()) + 2.f * kBoxPaddingY);
}

// Text baseline (logical px from the top of a note box) for the note font, so
// the countdown digits and the content text can share a single baseline.
float etaBaselineY(double pointSize) {
    const QFontMetricsF metrics{etaFont(pointSize)};
    return etaBoxHeight(pointSize) / 2.f +
            (static_cast<float>(metrics.ascent()) -
                    static_cast<float>(metrics.descent())) /
                    2.f;
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
        : ::WaveformRendererAbstract(waveformWidget) {
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
        // Parent for the live-ETA bar (background + content), drawn below the
        // digits. The bar node itself is created lazily in update() (it needs a
        // GL context to upload its texture).
        auto pNode = std::make_unique<Node>();
        m_pEtaBarNodesParent = pNode.get();
        appendChildNode(std::move(pNode));
    }
    {
        // Drawn last (on top) -- parent of the per-note live-ETA countdown digit
        // nodes at the play marker. The digit nodes are created lazily in update()
        // (they need a GL context to upload their atlas texture).
        auto pNode = std::make_unique<Node>();
        m_pEtaDigitsParent = pNode.get();
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
    // Note colors are driven by the preferences (concept section 9), not the
    // skin, so there is nothing to read here.
    Q_UNUSED(node);
    Q_UNUSED(skinContext);
}

void allshader::WaveformRenderNotes::refreshSettings() {
    auto* pFactory = WaveformWidgetFactory::instance();
    if (!pFactory) {
        return;
    }
    m_etaEnabled = pFactory->getEtaNotesEnabled();
    m_etaFontPointSize = pFactory->getEtaFontPointSize();
    m_etaShowBeats = pFactory->getEtaShowBeats();
    m_etaShowTime = pFactory->getEtaShowTime();
    m_etaAlignRightEdgeAtPlayhead = pFactory->getEtaAlignRightEdgeAtPlayhead();
    m_etaWindowBeats = pFactory->getEtaWindowBeats();
    m_etaAfterglowBeats = pFactory->getEtaAfterglowBeats();
    m_etaAfterglowOpacity = static_cast<float>(pFactory->getEtaAfterglowOpacity());
    m_etaNoteWidthPx = static_cast<float>(pFactory->getEtaNoteWidthPx());
    m_ownScheme = pFactory->getEtaColorScheme(EtaColorCase::Own);
}

QImage allshader::WaveformRenderNotes::bakeLabel(
        const QString& content, float devicePixelRatio) const {
    QString text = content.simplified(); // single line for now; markdown later
    if (text.isEmpty()) {
        text = QStringLiteral("(empty)"); // placeholder for an empty note
    }

    const QFont font = etaFont(m_etaFontPointSize);
    const QFontMetricsF metrics{font};

    const float w = std::ceil(
            static_cast<float>(metrics.horizontalAdvance(text)) + 2.f * kBoxPaddingX);
    const float h = etaBoxHeight(m_etaFontPointSize);

    QImage image(static_cast<int>(std::lround(w * devicePixelRatio)),
            static_cast<int>(std::lround(h * devicePixelRatio)),
            QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);

    QColor background = m_ownScheme.bgNormal;
    background.setAlphaF(0.85f);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), 3.0, 3.0);
    painter.setFont(font);
    painter.setPen(m_ownScheme.fontNormal);
    painter.drawText(
            QRectF(kBoxPaddingX, kBoxPaddingY, w - 2.f * kBoxPaddingX, h - 2.f * kBoxPaddingY),
            Qt::AlignLeft | Qt::AlignVCenter,
            text);
    painter.end();

    return image;
}

QImage allshader::WaveformRenderNotes::bakeEtaBar(const QString& content,
        float fieldWidth,
        float totalWidth,
        float opacity,
        const QColor& bgColor,
        const QColor& fontColor,
        double fontPointSize,
        float devicePixelRatio) const {
    QString text = content.simplified();
    if (text.isEmpty()) {
        text = QStringLiteral("(empty)");
    }

    const QFont font = etaFont(fontPointSize);
    const QFontMetricsF metrics{font};
    const float h = etaBoxHeight(fontPointSize);

    // One continuous bar: [ countdown field | inner gap | content | padding ].
    // The field region (width fieldWidth, on the left) is left empty here; the
    // live digits are drawn on top of it each frame. The content is elided to the
    // remaining width (wrapping is a later step).
    const float contentX = fieldWidth + kEtaInnerGap;
    const float contentWidth = std::max(0.f, totalWidth - contentX - kBoxPaddingX);
    text = metrics.elidedText(text, Qt::ElideRight, contentWidth);

    QImage image(static_cast<int>(std::lround(totalWidth * devicePixelRatio)),
            static_cast<int>(std::lround(h * devicePixelRatio)),
            QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);

    QColor background = bgColor;
    background.setAlphaF(0.85f * opacity);
    QColor textColor = fontColor;
    textColor.setAlphaF(opacity);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(0.5, 0.5, totalWidth - 1.0, h - 1.0), 3.0, 3.0);
    painter.setFont(font);
    painter.setPen(textColor);
    painter.drawText(QPointF(contentX, etaBaselineY(fontPointSize)), text);
    painter.end();

    return image;
}

allshader::WaveformRenderNotes::EtaBarSlot&
allshader::WaveformRenderNotes::ensureEtaBarSlot(
        int index,
        rendergraph::Context* pContext,
        const QString& content,
        float fieldWidth,
        float totalWidth,
        float opacity,
        float devicePixelRatio) {
    const auto bakeNeutral = [&]() {
        return bakeEtaBar(content, fieldWidth, totalWidth, opacity,
                m_ownScheme.bgNormal, m_ownScheme.fontNormal,
                m_etaFontPointSize, devicePixelRatio);
    };
    const auto bakeContrast = [&]() {
        return bakeEtaBar(content, fieldWidth, totalWidth, opacity,
                m_ownScheme.bgContrast, m_ownScheme.fontContrast,
                m_etaFontPointSize, devicePixelRatio);
    };

    // Grow the pool up to `index`. Because update() requests the slots in order
    // (0, 1, 2, ...), the loop only ever appends the single missing slot at the
    // end; it is baked with this call's inputs, which are exactly the inputs for
    // slot `index`.
    while (static_cast<int>(m_etaBarSlots.size()) <= index) {
        auto pNode = std::make_unique<NoteLabelNode>(pContext, bakeNeutral());
        auto pContrastNode = std::make_unique<NoteLabelNode>(pContext, bakeContrast());
        EtaBarSlot slot;
        slot.pNode = pNode.get();
        slot.pContrastNode = pContrastNode.get();
        slot.content = content;
        slot.fieldWidth = fieldWidth;
        slot.totalWidth = totalWidth;
        slot.opacity = opacity;
        slot.devicePixelRatio = devicePixelRatio;
        slot.fontPointSize = m_etaFontPointSize;
        slot.scheme = m_ownScheme;
        m_etaBarSlots.push_back(slot);
        m_pEtaBarNodesParent->appendChildNode(std::move(pNode));
        m_pEtaBarNodesParent->appendChildNode(std::move(pContrastNode));
    }
    EtaBarSlot& slot = m_etaBarSlots[index];
    if (slot.content != content || slot.fieldWidth != fieldWidth ||
            slot.totalWidth != totalWidth || slot.opacity != opacity ||
            slot.devicePixelRatio != devicePixelRatio ||
            slot.fontPointSize != m_etaFontPointSize || slot.scheme != m_ownScheme) {
        slot.pNode->updateTexture(pContext, bakeNeutral());
        slot.pContrastNode->updateTexture(pContext, bakeContrast());
        slot.content = content;
        slot.fieldWidth = fieldWidth;
        slot.totalWidth = totalWidth;
        slot.opacity = opacity;
        slot.devicePixelRatio = devicePixelRatio;
        slot.fontPointSize = m_etaFontPointSize;
        slot.scheme = m_ownScheme;
    }
    return slot;
}

allshader::DigitsRenderNode* allshader::WaveformRenderNotes::ensureEtaDigitNode(int index) {
    while (static_cast<int>(m_etaDigitNodes.size()) <= index) {
        auto pNode = std::make_unique<DigitsRenderNode>();
        m_etaDigitNodes.push_back(pNode.get());
        m_pEtaDigitsParent->appendChildNode(std::move(pNode));
    }
    return m_etaDigitNodes[index];
}

allshader::DigitsRenderNode* allshader::WaveformRenderNotes::ensureEtaDigitContrastNode(
        int index) {
    while (static_cast<int>(m_etaDigitContrastNodes.size()) <= index) {
        auto pNode = std::make_unique<DigitsRenderNode>();
        m_etaDigitContrastNodes.push_back(pNode.get());
        m_pEtaDigitsParent->appendChildNode(std::move(pNode));
    }
    return m_etaDigitContrastNodes[index];
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
    const bool playing = m_pPlayControl && m_pPlayControl->get() != 0.0;

    // Rebuilt below for the standing view; stays empty while playing (no labels
    // to click). Used by noteAtPoint() for the editor's hit-testing.
    m_noteHitBoxes.clear();

    // Pull the latest preferences (concept section 9) so changes apply next frame.
    refreshSettings();

    const auto hideAllEtaNodes = [this]() {
        for (auto& slot : m_etaBarSlots) {
            if (slot.pNode) {
                slot.pNode->hideQuad();
            }
            if (slot.pContrastNode) {
                slot.pContrastNode->hideQuad();
            }
        }
        for (auto* pDigits : m_etaDigitNodes) {
            pDigits->clear();
        }
        for (auto* pDigits : m_etaDigitContrastNodes) {
            pDigits->clear();
        }
    };

    // Master visibility switch (concept section 9): when off, the plugin draws
    // nothing on the waveform (the only entry point left is the settings page).
    if (!m_etaEnabled) {
        m_pLinesNode->geometry().allocate(0);
        m_pLinesNode->markDirtyGeometry();
        for (auto* pLabel : m_labelNodes) {
            pLabel->hideQuad();
        }
        hideAllEtaNodes();
        return;
    }

    // --- marker lines: one vertical rectangle per note (like WaveformRenderBeat).
    // Drawn after the beat grid (see waveformwidget.cpp), so they cover grid lines
    // they sit on. While playing they take the indicator's contrast color (so the
    // fixed timecode markers read together with the live-ETA bars); when stopped
    // they keep the standard note color.
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
        m_pLinesNode->material().setUniform(
                1, playing ? m_ownScheme.bgContrast : m_ownScheme.bgNormal);
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
            breadth != m_cachedBreadth ||
            m_etaFontPointSize != m_cachedFontPointSize ||
            m_ownScheme != m_cachedOwnScheme) {
        rebuildLabels(notes, devicePixelRatio);
        m_cachedContents = contents;
        m_cachedDevicePixelRatio = devicePixelRatio;
        m_cachedBreadth = breadth;
        m_cachedFontPointSize = m_etaFontPointSize;
        m_cachedOwnScheme = m_ownScheme;
    }

    DEBUG_ASSERT(m_labelNodes.size() == static_cast<size_t>(notes.size()));
    const int labelCount =
            std::min(static_cast<int>(notes.size()), static_cast<int>(m_labelNodes.size()));

    const double playPosition =
            m_waveformRenderer->getTruePosSample(::WaveformRendererAbstract::Play);

    // --- standing view (concept section 6): labels anchored at their timecode.
    // Overlapping labels are stacked downward so they don't cover each other:
    // earliest-arriving on top, later ones slid underneath, but never past the
    // waveform bottom (section 10), matching the live-ETA view. Labels whose
    // x-ranges don't overlap all stay on the top row, so the common (sparse)
    // case looks unchanged.
    if (!playing) {
        const float boxHeight = etaBoxHeight(m_etaFontPointSize);
        const int maxRows = std::max(1,
                static_cast<int>(std::floor(
                        (breadth - kStackTopMargin + kStackGap) /
                        (boxHeight + kStackGap))));

        // Drawable labels (valid position) with their geometry, processed in
        // left-to-right (= chronological) order so the earliest lands on top.
        struct StandingLabel {
            int index; // into notes / m_labelNodes
            float x;   // label box left edge (logical px)
            float width;
        };
        std::vector<StandingLabel> labels;
        labels.reserve(labelCount);
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
            const float w = m_labelNodes[i]->textureWidth() / devicePixelRatio;
            labels.push_back({i, x, w});
        }
        std::sort(labels.begin(), labels.end(),
                [](const StandingLabel& a, const StandingLabel& b) {
                    return a.x < b.x;
                });

        // Right edge (logical px) of the last label placed in each row; a label
        // may join a row only to the right of it (plus a small gap).
        std::vector<float> rowRightEdge(
                maxRows, std::numeric_limits<float>::lowest());
        for (const StandingLabel& label : labels) {
            int row = -1;
            for (int r = 0; r < maxRows; ++r) {
                if (label.x >= rowRightEdge[r] + kStackGap) {
                    row = r;
                    break;
                }
            }
            if (row < 0) {
                // Every row is still occupied at this x: drop into the one that
                // frees up soonest (least overlap), keeping the stack in bounds.
                row = static_cast<int>(
                        std::min_element(rowRightEdge.begin(), rowRightEdge.end()) -
                        rowRightEdge.begin());
            }
            rowRightEdge[row] = label.x + label.width;
            const float y = roundToPixel(
                    kStackTopMargin + row * (boxHeight + kStackGap));
            m_labelNodes[label.index]->setQuad(label.x, y, devicePixelRatio);
            const float h =
                    m_labelNodes[label.index]->textureHeight() / devicePixelRatio;
            m_noteHitBoxes.push_back({notes[label.index],
                    QRectF(label.x, y, label.width, h),
                    label.x - 2.f});
        }
        hideAllEtaNodes();
        return;
    }

    // --- live-ETA view (concept section 7): the timecode-anchored labels are
    // hidden; instead a stack of bars is anchored at the play marker, one per note
    // inside the preview window (plus recently passed notes still in afterglow).
    for (int i = 0; i < labelCount; ++i) {
        m_labelNodes[i]->hideQuad();
    }

    // Gather the notes to display, each with its countdown state. Upcoming notes
    // are kept while within m_etaWindowBeats; passed notes while within
    // m_etaAfterglowBeats ("Nachleuchten"). Without a beat grid we fall back to
    // just the single nearest upcoming note (the pre-window behaviour).
    struct EtaItem {
        int noteIndex;
        double position;
        int beats;
        double beatsExact;
        double timeSec;
        bool passed;
    };
    std::vector<EtaItem> items;
    int fallbackIndex = -1;
    double fallbackPosition = std::numeric_limits<double>::max();
    for (int i = 0; i < labelCount; ++i) {
        const mixxx::audio::FramePos position = notes[i]->getPosition();
        if (!position.isValid()) {
            continue;
        }
        const double samplePosition = position.toEngineSamplePos();
        bool hasBeats = false;
        int beats = 0;
        double beatsExact = 0.0;
        double timeSec = 0.0;
        computeBeatsAndTime(
                playPosition, samplePosition, &hasBeats, &beats, &beatsExact, &timeSec);
        if (samplePosition >= playPosition + 1.0) {
            // Upcoming.
            if (!hasBeats) {
                if (samplePosition < fallbackPosition) {
                    fallbackPosition = samplePosition;
                    fallbackIndex = i;
                }
            } else if (m_etaWindowBeats <= 0 || beats <= m_etaWindowBeats) {
                items.push_back({i, samplePosition, beats, beatsExact, timeSec, false});
            }
        } else if (hasBeats && m_etaAfterglowBeats > 0 && -beats <= m_etaAfterglowBeats) {
            // Passed, still lingering.
            items.push_back({i, samplePosition, std::max(0, beats), beatsExact, 0.0, true});
        }
    }
    if (items.empty() && fallbackIndex >= 0) {
        bool hasBeats = false;
        int beats = 0;
        double beatsExact = 0.0;
        double timeSec = 0.0;
        computeBeatsAndTime(
                playPosition, fallbackPosition, &hasBeats, &beats, &beatsExact, &timeSec);
        items.push_back(
                {fallbackIndex, fallbackPosition, std::max(0, beats), beatsExact, timeSec, false});
    }

    if (items.empty()) {
        hideAllEtaNodes();
        return;
    }

    // Stack earliest-arriving on top (concept section 6: notes orient upward, the
    // later one slides underneath).
    std::sort(items.begin(), items.end(), [](const EtaItem& a, const EtaItem& b) {
        return a.position < b.position;
    });

    auto* pContext = m_waveformRenderer->getContext();
    const float boxHeight = etaBoxHeight(m_etaFontPointSize);
    const QColor textColor = m_ownScheme.fontNormal;

    // Shared digit atlas params (font, height, color). ensureEtaDigitNode(0) gives
    // us a node to measure the countdown-field columns with; updateTexture is a
    // no-op when the params are unchanged, so building all nodes is cheap.
    DigitsRenderNode* pAtlas = ensureEtaDigitNode(0);
    pAtlas->updateTexture(pContext,
            static_cast<float>(m_etaFontPointSize),
            boxHeight,
            devicePixelRatio,
            textColor,
            /*withOutline=*/false,
            /*fontFamily=*/QString());

    // One uniform countdown-field width for every bar, from worst-case-width
    // templates (each digit as the full-width '8'; the beats column sized to the
    // window's digit count). This keeps all bars' content aligned in a column and
    // stops the layout shifting as the live numbers count down.
    const float gapDigits = pAtlas->height() * kDigitsGapFactor;
    const int beatsDigits = std::max(2,
            static_cast<int>(QString::number(std::max(1, m_etaWindowBeats)).length()));
    const QString beatsTpl = m_etaShowBeats ? QString(beatsDigits, QChar('8')) : QString{};
    const QString timeTpl = m_etaShowTime ? QStringLiteral("8:88.88") : QString{};
    const float beatsColWidth =
            beatsTpl.isEmpty() ? 0.f : pAtlas->measure(beatsTpl, QString{}, false);
    const float timeColWidth =
            timeTpl.isEmpty() ? 0.f : pAtlas->measure(QString{}, timeTpl, false);
    const float innerGap = (beatsColWidth > 0.f && timeColWidth > 0.f) ? gapDigits : 0.f;
    const float fieldWidth = (beatsColWidth > 0.f || timeColWidth > 0.f)
            ? kFieldPadX + beatsColWidth + innerGap + timeColWidth + kFieldPadX
            : 0.f;

    const float playMarkerPos = static_cast<float>(
            m_waveformRenderer->getPlayMarkerPosition() *
            m_waveformRenderer->getLength());

    // The proximity indicator (concept section 7) needs a fixed bar width (section
    // 9) so it fills equally across notes; with a content-sized bar it is disabled.
    const bool fixedWidth = m_etaNoteWidthPx > 0.f;
    const bool indicatorEnabled = fixedWidth && m_etaWindowBeats > 0;
    const QColor contrastFontColor = m_ownScheme.fontContrast;

    int shown = 0;
    for (int k = 0; k < static_cast<int>(items.size()); ++k) {
        const float boxTop = roundToPixel(kStackTopMargin + k * (boxHeight + kStackGap));
        if (boxTop + boxHeight > breadth) {
            // Would run past the bottom of the waveform: drop this and all the
            // (lower) bars below it (concept section 10).
            break;
        }
        const EtaItem& item = items[k];
        const float opacity = item.passed ? m_etaAfterglowOpacity : 1.f;
        const QString content = notes[item.noteIndex]->getContent();

        float totalWidth = m_etaNoteWidthPx;
        if (!fixedWidth) {
            QString t = content.simplified();
            if (t.isEmpty()) {
                t = QStringLiteral("(empty)");
            }
            const QFontMetricsF metrics{etaFont(m_etaFontPointSize)};
            totalWidth = fieldWidth + kEtaInnerGap +
                    std::ceil(static_cast<float>(metrics.horizontalAdvance(t))) +
                    kBoxPaddingX;
        }

        EtaBarSlot& slot = ensureEtaBarSlot(
                k, pContext, content, fieldWidth, totalWidth, opacity, devicePixelRatio);
        const float barWidth = slot.pNode->textureWidth() / devicePixelRatio;
        const float blockLeft = roundToPixel(
                m_etaAlignRightEdgeAtPlayhead ? playMarkerPos - barWidth : playMarkerPos);
        slot.pNode->setQuad(blockLeft, boxTop, devicePixelRatio);

        // Reveal the contrast bar from the right, proportional to how far the note
        // has travelled through the preview window: empty at the window edge, full
        // when it reaches the play marker, and held full while it lingers.
        float fill = 0.f;
        if (indicatorEnabled) {
            fill = item.passed
                    ? 1.f
                    : std::clamp(static_cast<float>((m_etaWindowBeats - item.beatsExact) /
                                         m_etaWindowBeats),
                              0.f,
                              1.f);
        }
        // Boundary x where the contrast fill begins (same as the bar's contrast
        // quad left edge), so the digits change color exactly there.
        const float fillBoundaryX = blockLeft + barWidth * (1.f - fill);
        if (fill > 0.f) {
            slot.pContrastNode->setQuadClippedLeft(blockLeft, boxTop, fill, devicePixelRatio);
        } else {
            slot.pContrastNode->hideQuad();
        }

        DigitsRenderNode* pDigits = ensureEtaDigitNode(k);
        DigitsRenderNode* pDigitsContrast = ensureEtaDigitContrastNode(k);
        pDigits->updateTexture(pContext,
                static_cast<float>(m_etaFontPointSize),
                boxHeight,
                devicePixelRatio,
                textColor,
                /*withOutline=*/false,
                /*fontFamily=*/QString());
        pDigitsContrast->updateTexture(pContext,
                static_cast<float>(m_etaFontPointSize),
                boxHeight,
                devicePixelRatio,
                contrastFontColor,
                /*withOutline=*/false,
                /*fontFamily=*/QString());
        if (item.passed) {
            // The countdown is over; the dimmed bar lingers without a number.
            pDigits->clear();
            pDigitsContrast->clear();
        } else {
            const QString beatsStr =
                    m_etaShowBeats ? QString::number(item.beats) : QString{};
            const QString timeStr =
                    m_etaShowTime ? timeSecToString(item.timeSec) : QString{};
            // Beats right-aligned against the fixed column edge (so the time after
            // them stays put), time immediately after at a fixed offset.
            const float beatsWidth =
                    beatsStr.isEmpty() ? 0.f : pDigits->measure(beatsStr, QString{}, false);
            const float digitsX =
                    roundToPixel(blockLeft + kFieldPadX + (beatsColWidth - beatsWidth));
            const float digitsY =
                    roundToPixel(boxTop + etaBaselineY(m_etaFontPointSize) - pDigits->baseline());
            // Draw the same digits twice, clipped at the fill boundary: neutral on
            // the not-yet-reached (left) part, contrast on the filled (right) part.
            pDigits->updateClipped(digitsX, digitsY, false, beatsStr, timeStr,
                    std::numeric_limits<float>::lowest(), fillBoundaryX);
            pDigitsContrast->updateClipped(digitsX, digitsY, false, beatsStr, timeStr,
                    fillBoundaryX, std::numeric_limits<float>::max());
        }
        ++shown;
    }

    // Hide the unused tail of both pools.
    for (int k = shown; k < static_cast<int>(m_etaBarSlots.size()); ++k) {
        if (m_etaBarSlots[k].pNode) {
            m_etaBarSlots[k].pNode->hideQuad();
        }
        if (m_etaBarSlots[k].pContrastNode) {
            m_etaBarSlots[k].pContrastNode->hideQuad();
        }
    }
    for (int k = shown; k < static_cast<int>(m_etaDigitNodes.size()); ++k) {
        m_etaDigitNodes[k]->clear();
    }
    for (int k = shown; k < static_cast<int>(m_etaDigitContrastNodes.size()); ++k) {
        m_etaDigitContrastNodes[k]->clear();
    }
}

NotePointer allshader::WaveformRenderNotes::noteAtPoint(QPointF point) const {
    // A little slack around the thin marker line so it is easy to hit, in
    // addition to the label rectangle itself.
    constexpr float kLineHitTolerance = 3.f;
    for (auto it = m_noteHitBoxes.crbegin(); it != m_noteHitBoxes.crend(); ++it) {
        if (it->labelRect.contains(point) ||
                std::abs(static_cast<float>(point.x()) - it->lineX) <=
                        kLineHitTolerance) {
            return it->note;
        }
    }
    return {};
}

void allshader::WaveformRenderNotes::computeBeatsAndTime(double playPosition,
        double notePosition,
        bool* hasBeats,
        int* beats,
        double* beatsExact,
        double* timeSec) const {
    *hasBeats = false;
    *beats = 0;
    *beatsExact = 0.0;
    *timeSec = 0.0;

    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();
    if (!trackInfo) {
        return;
    }

    const double endPosition = m_waveformRenderer->getTrackSamples();
    const double remainingTime =
            m_pTimeRemainingControl ? m_pTimeRemainingControl->get() : 0.0;

    // Full beats remaining to the note, floored: the count drops to 0 as soon as
    // less than one whole beat is left (Raik's request), rather than rounding to
    // the nearest beat like WaveformRenderMark::updateUntilMark does. This needs
    // the fractional beat position of both the play and the note position, so a
    // partly-consumed beat is not counted. The signed result is positive for an
    // upcoming note and negative for one that has already passed.
    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (trackBeats) {
        // The beat at or before a position, plus the fraction (0..1) of how far
        // the position lies into that beat.
        const auto beatBeforeWithFraction =
                [&trackBeats](double pos, double* fraction) {
                    auto it = trackBeats->iteratorFrom(
                            mixxx::audio::FramePos::fromEngineSamplePos(pos));
                    // iteratorFrom returns the first beat at or after pos; step
                    // back to the beat at or before it (tolerate ~1 sample).
                    if (it->toEngineSamplePos() > pos + 1.0) {
                        it = it - 1;
                    }
                    const double prev = it->toEngineSamplePos();
                    const double next = (it + 1)->toEngineSamplePos();
                    *fraction = (next > prev)
                            ? std::clamp((pos - prev) / (next - prev), 0.0, 1.0)
                            : 0.0;
                    return it;
                };

        double fracPlay = 0.0;
        double fracNote = 0.0;
        const auto itPlay = beatBeforeWithFraction(playPosition, &fracPlay);
        const auto itNote = beatBeforeWithFraction(notePosition, &fracNote);
        const double wholeBeats = static_cast<double>(itNote - itPlay);
        *beatsExact = wholeBeats - fracPlay + fracNote;
        *beats = static_cast<int>(std::floor(*beatsExact));
        *hasBeats = true;
    }

    // As endPosition - playPosition corresponds with remainingTime, take the
    // proportional part up to notePosition (>= 0 for upcoming notes).
    if (endPosition > playPosition) {
        *timeSec = std::max(0.0,
                remainingTime * (notePosition - playPosition) /
                        (endPosition - playPosition));
    }
}
