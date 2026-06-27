#include "waveform/renderers/allshader/digitsrenderer.h"

#include <QColor>
#include <QFontMetricsF>
#include <algorithm>
#include <limits>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "rendergraph/context.h"
#include "rendergraph/geometry.h"
#include "rendergraph/material/texturematerial.h"
#include "rendergraph/vertexupdaters/texturedvertexupdater.h"
#include "util/assert.h"
#include "util/roundtopixel.h"

// Render digits using a texture (generated) with digits with blurred dark outline

using namespace rendergraph;

namespace {

// The texture will contain 12 characters: 10 digits, colon and dot
constexpr int NUM_CHARS = 12;

// space around chars for blurred dark outline
constexpr int OUTLINE_SIZE = 4;
// alpha of the blurred dark outline
constexpr int OUTLINE_ALPHA = 224;

constexpr char indexToChar(int index) {
    constexpr char str[] = "0123456789:.";
    return str[index];
}
constexpr int charToIndex(QChar ch) {
    int value = ch.toLatin1() - '0';
    if (value >= 0 && value <= 9) {
        return value;
    }
    if (ch == ':') {
        return 10;
    }
    if (ch == '.') {
        return 11;
    }
    DEBUG_ASSERT(false);
    return 11; // fallback to dot
}
constexpr bool checkCharToIndex() {
    for (int i = 0; i < NUM_CHARS; i++) {
        if (charToIndex(indexToChar(i)) != i) {
            return false;
        }
    }
    return true;
}
static_assert(checkCharToIndex());

} // namespace

allshader::DigitsRenderNode::DigitsRenderNode() {
    setGeometry(std::make_unique<Geometry>(TextureMaterial::attributes(), 0));
    setMaterial(std::make_unique<TextureMaterial>());
    geometry().setDrawingMode(Geometry::DrawingMode::Triangles);
}

allshader::DigitsRenderNode::~DigitsRenderNode() = default;

float allshader::DigitsRenderNode::height() const {
    return m_height;
}

float allshader::DigitsRenderNode::baseline() const {
    return m_baseline;
}

void allshader::DigitsRenderNode::updateTexture(rendergraph::Context* pContext,
        float fontPointSize,
        float maxHeight,
        float devicePixelRatio,
        const QColor& textColor,
        bool withOutline,
        const QString& fontFamily) {
    if (fontPointSize == m_fontPointSize && maxHeight == m_maxHeight &&
            textColor == m_textColor && withOutline == m_withOutline &&
            fontFamily == m_fontFamily) {
        return;
    }
    m_textColor = textColor;
    m_withOutline = withOutline;
    m_fontFamily = fontFamily;
    if (maxHeight != m_maxHeight) {
        m_maxHeight = maxHeight;
        m_adjustedFontPointSize = 0.f;
    }
    if (m_fontPointSize != fontPointSize) {
        m_fontPointSize = fontPointSize;
        if (m_adjustedFontPointSize != 0.f && fontPointSize > m_adjustedFontPointSize) {
            fontPointSize = m_adjustedFontPointSize;
        } else {
            m_adjustedFontPointSize = 0.f;
        }
    }

    float space;
    // Vertical margin above/below the glyphs in the atlas. With an outline it is
    // the outline space; without one we still keep a small margin so ascenders
    // are not clipped at the texture edge (horizontal packing stays tight).
    float vSpace;

    QFont font;
    QFontMetricsF metrics{font};
    if (!m_fontFamily.isEmpty()) {
        font.setFamily(m_fontFamily);
    }
    float maxTextHeight;
    bool retry = false;
    do {
        // At small sizes, we need to limit the pen width, to avoid drawing artifacts.
        // (The factor 0.25 was found with trial and error)
        const int maxPenWidth = 1 + std::lround(fontPointSize * 0.25f);
        // The pen width is twice the outline size. Without an outline there is
        // no surrounding space, so the digits pack like normal text.
        m_penWidth = m_withOutline ? std::min(maxPenWidth, OUTLINE_SIZE * 2) : 0;

        space = static_cast<float>(m_penWidth) / 2;
        vSpace = m_withOutline ? space : 1.5f;
        font.setPointSizeF(fontPointSize);

        const float maxHeightWithoutSpace = std::floor(maxHeight) - vSpace * 2 - 1;

        metrics = QFontMetricsF{font};

        maxTextHeight = 0;

        for (int i = 0; i < NUM_CHARS; i++) {
            const QString text(indexToChar(i));
            const auto rect = metrics.tightBoundingRect(text);
            maxTextHeight = std::max(maxTextHeight, static_cast<float>(rect.height()));
        }
        if (m_adjustedFontPointSize == 0.f && !retry && maxTextHeight > maxHeightWithoutSpace) {
            // We need to adjust the font size to fit in the maxHeight.
            // Only do this once.
            fontPointSize *= static_cast<float>(maxHeightWithoutSpace / maxTextHeight);
            // Avoid becoming unreadable
            fontPointSize = std::max(10.f, fontPointSize);
            m_adjustedFontPointSize = fontPointSize;
            retry = true;
        } else {
            retry = false;
        }
    } while (retry);

    m_height = static_cast<float>(std::ceil(maxTextHeight)) + vSpace * 2.f + 1.f;

    const float y = maxTextHeight + vSpace - 0.5f;
    m_baseline = y;

    auto roundToPixel = createFunctionRoundToPixel(devicePixelRatio);

    float totalTextWidth{};
    std::array<float, NUM_CHARS> xs;
    // determine x position and with of each of the chars in the texture image.
    for (int i = 0; i < NUM_CHARS; i++) {
        xs[i] = totalTextWidth;
        float w = roundToPixel(static_cast<float>(
                          metrics.horizontalAdvance(indexToChar(i)))) +
                space + space + 1.f;
        totalTextWidth += w;
        m_width[i] = static_cast<float>(w);
    }
    for (int i = 0; i < NUM_CHARS; i++) {
        // position of character at index i in the texture, normalized
        m_offset[i] = static_cast<float>(xs[i] / totalTextWidth);
    }
    m_offset[NUM_CHARS] = 1.f;

    QImage image(std::lround(totalTextWidth * devicePixelRatio),
            std::lround(m_height * devicePixelRatio),
            QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(Qt::transparent);

    if (m_withOutline) {
        // Draw digits with dark outline
        QPainter painter(&image);

        QPen pen(QColor(0, 0, 0, OUTLINE_ALPHA));
        pen.setWidth(m_penWidth);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(0, 0, 0, OUTLINE_ALPHA));
        painter.setPen(pen);
        painter.setFont(font);
        QPainterPath path;
        for (int i = 0; i < NUM_CHARS; i++) {
            const QString text(indexToChar(i));
            path.addText(QPointF(xs[i] + space + 0.5, y), font, text);
        }
        painter.drawPath(path);

        // Apply Gaussian blur to dark outline
        auto blur = std::make_unique<QGraphicsBlurEffect>();
        blur->setBlurRadius(static_cast<float>(m_penWidth) / 3);

        QGraphicsScene scene;
        QGraphicsPixmapItem item;
        item.setPixmap(QPixmap::fromImage(image));
        item.setGraphicsEffect(blur.release());
        image.fill(Qt::transparent);
        QPainter blurPainter(&image);
        scene.addItem(&item);
        scene.render(&blurPainter, QRectF(), QRectF(0, 0, image.width(), image.height()));
    }

    {
        // Draw digits foreground
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);

        painter.setFont(font);
        // Stroking the glyph path (as the outlined style does) thickens the
        // digits; without an outline, fill only for a normal text weight.
        painter.setPen(m_withOutline ? QPen(m_textColor) : QPen(Qt::NoPen));
        painter.setBrush(m_textColor);

        QPainterPath path;
        for (int i = 0; i < NUM_CHARS; i++) {
            const QString text(indexToChar(i));
            path.addText(QPointF(xs[i] + space + 0.5, y), font, text);
        }
        painter.drawPath(path);
    }

    dynamic_cast<TextureMaterial&>(material())
            .setTexture(std::make_unique<Texture>(pContext, image));
}

void allshader::DigitsRenderNode::update(
        float x,
        float y,
        bool multiLine,
        const QString& s1,
        const QString& s2) {
    updateClipped(x,
            y,
            multiLine,
            s1,
            s2,
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::max());
}

void allshader::DigitsRenderNode::updateClipped(
        float x,
        float y,
        bool multiLine,
        const QString& s1,
        const QString& s2,
        float clipLeft,
        float clipRight) {
    const int numVerticesPerRectangle = 6;
    const int reserved = (s1.length() + s2.length()) * numVerticesPerRectangle;
    geometry().allocate(reserved);
    TexturedVertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::TexturedPoint2D>()};

    const float ch = height();
    if (!s1.isEmpty()) {
        const auto w = addVertices(vertexUpdater,
                x,
                y,
                s1,
                clipLeft,
                clipRight);
        if (multiLine) {
            y += ch;
        } else {
            x += w + ch * 0.75f;
        }
    }
    if (!s2.isEmpty()) {
        addVertices(vertexUpdater,
                x,
                y,
                s2,
                clipLeft,
                clipRight);
    }

    DEBUG_ASSERT(reserved == vertexUpdater.index());
}

void allshader::DigitsRenderNode::clear() {
    geometry().allocate(0);
}

float allshader::DigitsRenderNode::measure(
        const QString& s1, const QString& s2, bool multiLine) const {
    const float space = static_cast<float>(m_penWidth) / 2;
    // Mirrors addVertices: characters are packed with a small overlap of `space`
    // between adjacent glyphs.
    const auto widthOf = [this, space](const QString& s) {
        float x = 0.f;
        bool first = true;
        for (QChar c : s) {
            if (!first) {
                x -= space;
            }
            first = false;
            x += m_width[charToIndex(c)];
        }
        return x;
    };
    const float w1 = s1.isEmpty() ? 0.f : widthOf(s1);
    const float w2 = s2.isEmpty() ? 0.f : widthOf(s2);
    if (multiLine) {
        return std::max(w1, w2);
    }
    // Single line: the two strings are laid out side by side with a gap of
    // height() * 0.75 between them (see update()).
    if (w1 > 0.f && w2 > 0.f) {
        return w1 + height() * 0.75f + w2;
    }
    return w1 + w2;
}

float allshader::DigitsRenderNode::addVertices(TexturedVertexUpdater& vertexUpdater,
        float x,
        float y,
        const QString& s,
        float clipLeft,
        float clipRight) {
    const float x0 = x;
    const float space = static_cast<float>(m_penWidth) / 2;

    for (QChar c : s) {
        if (x != x0) {
            x -= space;
        }
        int index = charToIndex(c);
        const float w = m_width[index];

        // Intersect the glyph [x, x+w] with the clip range; a straddling glyph is
        // cut exactly at the edge, with the texture u-coordinates moved to match,
        // so the rendered part lines up to the pixel. Glyphs fully outside emit a
        // degenerate (invisible) rectangle to keep the reserved vertex count exact.
        const float left = std::max(x, clipLeft);
        const float right = std::min(x + w, clipRight);
        if (w > 0.f && right > left) {
            const float u0 = m_offset[index];
            const float u1 = m_offset[index + 1];
            vertexUpdater.addRectangle({left, y},
                    {right, y + height()},
                    {u0 + (u1 - u0) * (left - x) / w, 0.f},
                    {u0 + (u1 - u0) * (right - x) / w, 1.f});
        } else {
            vertexUpdater.addRectangle({0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f}, {0.f, 0.f});
        }
        x += w;
    }

    return x - x0;
}
