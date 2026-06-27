#pragma once

#include <QColor>
#include <QString>

#include "rendergraph/context.h"
#include "rendergraph/geometrynode.h"
#include "util/class.h"

namespace rendergraph {
class TexturedVertexUpdater;
} // namespace rendergraph

namespace allshader {
class DigitsRenderNode;
} // namespace allshader

class allshader::DigitsRenderNode : public rendergraph::GeometryNode {
  public:
    DigitsRenderNode();
    ~DigitsRenderNode();

    // The default style (white fill with a blurred dark outline, Open Sans) is
    // used by the cue until-mark display. Callers can override the text color,
    // disable the outline, and pick a font family (empty = application default)
    // so the digits can match surrounding text, e.g. the ETA note labels.
    void updateTexture(rendergraph::Context* pContext,
            float fontPointSize,
            float maxHeight,
            float devicePixelRatio,
            const QColor& textColor = QColor(Qt::white),
            bool withOutline = true,
            const QString& fontFamily = QStringLiteral("Open Sans"));

    void update(
            float x,
            float y,
            bool multiLine,
            const QString& s1,
            const QString& s2);

    // Like update(), but only the parts of the glyphs within the horizontal range
    // [clipLeft, clipRight) are drawn; a glyph straddling an edge is split exactly
    // there (texture coordinates adjusted). Two clipped passes in different colors
    // give a pixel-perfect color change at an arbitrary x (the ETA proximity fill).
    void updateClipped(
            float x,
            float y,
            bool multiLine,
            const QString& s1,
            const QString& s2,
            float clipLeft,
            float clipRight);

    void clear();

    float height() const;

    // Distance in logical pixels from the top of the rendered block to the text
    // baseline, so callers can baseline-align the digits with adjacent text.
    float baseline() const;

    // Width in logical pixels that update() would occupy for the given strings,
    // using the same layout. Valid after updateTexture has been called at least
    // once. Used by callers that need to right-align the rendered block.
    float measure(const QString& s1, const QString& s2, bool multiLine) const;

  private:
    float addVertices(rendergraph::TexturedVertexUpdater& vertexUpdater,
            float x,
            float y,
            const QString& s,
            float clipLeft,
            float clipRight);

    int m_penWidth;
    float m_offset[13];
    float m_width[12];
    float m_fontPointSize{};
    float m_height{};
    float m_baseline{};
    float m_maxHeight{};
    float m_adjustedFontPointSize{};
    QColor m_textColor{Qt::white};
    bool m_withOutline{true};
    QString m_fontFamily{QStringLiteral("Open Sans")};
    DISALLOW_COPY_AND_ASSIGN(DigitsRenderNode);
};
