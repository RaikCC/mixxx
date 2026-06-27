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

// One shared font size for the note labels and the live-ETA countdown digits,
// so the two read as a single unit (concept section 9 has a single note font
// size; the preferences UI in phase 2d will drive this). Padding around the
// text inside the rounded boxes.
constexpr double kEtaFontPointSize = 10.0;
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

QFont etaFont() {
    QFont font;
    font.setPointSizeF(kEtaFontPointSize);
    return font;
}

// Logical height of a note box (label or countdown field) for the shared font.
float etaBoxHeight() {
    const QFontMetricsF metrics{etaFont()};
    return std::ceil(static_cast<float>(metrics.height()) + 2.f * kBoxPaddingY);
}

// Text baseline (logical px from the top of a note box) for the shared font, so
// the countdown digits and the content text can share a single baseline.
float etaBaselineY() {
    const QFontMetricsF metrics{etaFont()};
    return etaBoxHeight() / 2.f +
            (static_cast<float>(metrics.ascent()) -
                    static_cast<float>(metrics.descent())) /
                    2.f;
}

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

    const QFont font = etaFont();
    const QFontMetricsF metrics{font};

    const float w = std::ceil(
            static_cast<float>(metrics.horizontalAdvance(text)) + 2.f * kBoxPaddingX);
    const float h = etaBoxHeight();

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
            QRectF(kBoxPaddingX, kBoxPaddingY, w - 2.f * kBoxPaddingX, h - 2.f * kBoxPaddingY),
            Qt::AlignLeft | Qt::AlignVCenter,
            text);
    painter.end();

    return image;
}

QImage allshader::WaveformRenderNotes::bakeEtaBar(
        const QString& content, float fieldWidth, float opacity, float devicePixelRatio) const {
    QString text = content.simplified();
    if (text.isEmpty()) {
        text = QStringLiteral("(empty)");
    }

    const QFont font = etaFont();
    const QFontMetricsF metrics{font};
    const float h = etaBoxHeight();
    const float textWidth =
            std::ceil(static_cast<float>(metrics.horizontalAdvance(text)));

    // One continuous bar: [ countdown field | inner gap | content | padding ].
    // The field region (width fieldWidth, on the left) is left empty here; the
    // live digits are drawn on top of it each frame.
    constexpr float kInnerGap = 6.f;
    const float width = fieldWidth + kInnerGap + textWidth + kBoxPaddingX;

    QImage image(static_cast<int>(std::lround(width * devicePixelRatio)),
            static_cast<int>(std::lround(h * devicePixelRatio)),
            QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);

    QColor background = m_color;
    background.setAlphaF(0.85f * opacity);
    QColor textColor = contrastingTextColor(m_color);
    textColor.setAlphaF(opacity);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(0.5, 0.5, width - 1.0, h - 1.0), 3.0, 3.0);
    painter.setFont(font);
    painter.setPen(textColor);
    painter.drawText(QPointF(fieldWidth + kInnerGap, etaBaselineY()), text);
    painter.end();

    return image;
}

allshader::NoteLabelNode* allshader::WaveformRenderNotes::ensureEtaBarNode(
        int index,
        rendergraph::Context* pContext,
        const QString& content,
        float fieldWidth,
        float opacity,
        float devicePixelRatio) {
    // Grow the pool up to `index`. Because update() requests the slots in order
    // (0, 1, 2, ...), the loop only ever appends the single missing node at the
    // end; it is baked with this call's inputs, which are exactly the inputs for
    // slot `index`.
    while (static_cast<int>(m_etaBarSlots.size()) <= index) {
        auto pNode = std::make_unique<NoteLabelNode>(
                pContext, bakeEtaBar(content, fieldWidth, opacity, devicePixelRatio));
        EtaBarSlot slot;
        slot.pNode = pNode.get();
        slot.content = content;
        slot.fieldWidth = fieldWidth;
        slot.opacity = opacity;
        slot.devicePixelRatio = devicePixelRatio;
        slot.color = m_color;
        m_etaBarSlots.push_back(slot);
        m_pEtaBarNodesParent->appendChildNode(std::move(pNode));
    }
    EtaBarSlot& slot = m_etaBarSlots[index];
    if (slot.content != content || slot.fieldWidth != fieldWidth ||
            slot.opacity != opacity || slot.devicePixelRatio != devicePixelRatio ||
            slot.color != m_color) {
        slot.pNode->updateTexture(
                pContext, bakeEtaBar(content, fieldWidth, opacity, devicePixelRatio));
        slot.content = content;
        slot.fieldWidth = fieldWidth;
        slot.opacity = opacity;
        slot.devicePixelRatio = devicePixelRatio;
        slot.color = m_color;
    }
    return slot.pNode;
}

allshader::DigitsRenderNode* allshader::WaveformRenderNotes::ensureEtaDigitNode(int index) {
    while (static_cast<int>(m_etaDigitNodes.size()) <= index) {
        auto pNode = std::make_unique<DigitsRenderNode>();
        m_etaDigitNodes.push_back(pNode.get());
        m_pEtaDigitsParent->appendChildNode(std::move(pNode));
    }
    return m_etaDigitNodes[index];
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

    const double playPosition =
            m_waveformRenderer->getTruePosSample(::WaveformRendererAbstract::Play);
    const bool playing = m_pPlayControl && m_pPlayControl->get() != 0.0;

    const auto hideAllEtaNodes = [this]() {
        for (auto& slot : m_etaBarSlots) {
            if (slot.pNode) {
                slot.pNode->hideQuad();
            }
        }
        for (auto* pDigits : m_etaDigitNodes) {
            pDigits->clear();
        }
    };

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
        double timeSec = 0.0;
        computeBeatsAndTime(playPosition, samplePosition, &hasBeats, &beats, &timeSec);
        if (samplePosition >= playPosition + 1.0) {
            // Upcoming.
            if (!hasBeats) {
                if (samplePosition < fallbackPosition) {
                    fallbackPosition = samplePosition;
                    fallbackIndex = i;
                }
            } else if (m_etaWindowBeats <= 0 || beats <= m_etaWindowBeats) {
                items.push_back({i, samplePosition, beats, timeSec, false});
            }
        } else if (hasBeats && m_etaAfterglowBeats > 0 && -beats <= m_etaAfterglowBeats) {
            // Passed, still lingering.
            items.push_back({i, samplePosition, std::max(0, beats), 0.0, true});
        }
    }
    if (items.empty() && fallbackIndex >= 0) {
        bool hasBeats = false;
        int beats = 0;
        double timeSec = 0.0;
        computeBeatsAndTime(playPosition, fallbackPosition, &hasBeats, &beats, &timeSec);
        items.push_back({fallbackIndex, fallbackPosition, std::max(0, beats), timeSec, false});
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
    const float boxHeight = etaBoxHeight();
    const QColor textColor = contrastingTextColor(m_color);

    // Shared digit atlas params (font, height, color). ensureEtaDigitNode(0) gives
    // us a node to measure the countdown-field columns with; updateTexture is a
    // no-op when the params are unchanged, so building all nodes is cheap.
    DigitsRenderNode* pAtlas = ensureEtaDigitNode(0);
    pAtlas->updateTexture(pContext,
            static_cast<float>(kEtaFontPointSize),
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

        NoteLabelNode* pBar = ensureEtaBarNode(
                k, pContext, content, fieldWidth, opacity, devicePixelRatio);
        const float barWidth = pBar->textureWidth() / devicePixelRatio;
        const float blockLeft = roundToPixel(
                m_etaAlignRightEdgeAtPlayhead ? playMarkerPos - barWidth : playMarkerPos);
        pBar->setQuad(blockLeft, boxTop, devicePixelRatio);

        DigitsRenderNode* pDigits = ensureEtaDigitNode(k);
        pDigits->updateTexture(pContext,
                static_cast<float>(kEtaFontPointSize),
                boxHeight,
                devicePixelRatio,
                textColor,
                /*withOutline=*/false,
                /*fontFamily=*/QString());
        if (item.passed) {
            // The countdown is over; the dimmed bar lingers without a number.
            pDigits->clear();
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
                    roundToPixel(boxTop + etaBaselineY() - pDigits->baseline());
            pDigits->update(digitsX, digitsY, false, beatsStr, timeStr);
        }
        ++shown;
    }

    // Hide the unused tail of both pools.
    for (int k = shown; k < static_cast<int>(m_etaBarSlots.size()); ++k) {
        if (m_etaBarSlots[k].pNode) {
            m_etaBarSlots[k].pNode->hideQuad();
        }
    }
    for (int k = shown; k < static_cast<int>(m_etaDigitNodes.size()); ++k) {
        m_etaDigitNodes[k]->clear();
    }
}

void allshader::WaveformRenderNotes::computeBeatsAndTime(double playPosition,
        double notePosition,
        bool* hasBeats,
        int* beats,
        double* timeSec) const {
    *hasBeats = false;
    *beats = 0;
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
        *beats = static_cast<int>(std::floor(wholeBeats - fracPlay + fracNote));
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
