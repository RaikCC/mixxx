#pragma once

#include <QColor>
#include <QImage>
#include <QStringList>
#include <vector>

#include "rendergraph/node.h"
#include "track/track.h"
#include "util/class.h"
#include "waveform/renderers/waveformrendererabstract.h"

class QDomNode;
class SkinContext;

namespace rendergraph {
class GeometryNode;
} // namespace rendergraph

namespace allshader {
class WaveformRenderNotes;
class NoteLabelNode;
} // namespace allshader

/// Renders the track's ETA Notes (concept document section 4) on the waveform:
/// a vertical marker line at each note position plus a baked text-texture label
/// showing the note content. Modeled on allshader::WaveformRenderMark (marker
/// lines like WaveformRenderBeat, label textures like the mark/digits nodes).
///
/// Because creating and destroying textures requires a current OpenGL context,
/// the work is done in update(), which the WaveformWidget calls from paintGL()
/// (not in preprocess()). Label textures are only re-baked when the set of note
/// contents (or the device pixel ratio / height) changes; positions are updated
/// every frame.
class allshader::WaveformRenderNotes final
        : public QObject,
          public ::WaveformRendererAbstract,
          public rendergraph::Node {
    Q_OBJECT
  public:
    explicit WaveformRenderNotes(WaveformWidgetRenderer* waveformWidget,
            ::WaveformRendererAbstract::PositionSource type =
                    ::WaveformRendererAbstract::Play);

    // Pure virtual from WaveformRendererAbstract, not used (we render via the
    // scene graph, driven by update()).
    void draw(QPainter* painter, QPaintEvent* event) override final;

    void setup(const QDomNode& node, const SkinContext& skinContext) override;

    // Called from WaveformWidget::paintGL with a current OpenGL context.
    void update();

  public slots:
    void setColor(const QColor& color) {
        m_color = color;
    }

  private:
    QImage bakeLabel(const QString& content, float devicePixelRatio) const;
    void rebuildLabels(const QList<NotePointer>& notes, float devicePixelRatio);

    QColor m_color;

    rendergraph::GeometryNode* m_pLinesNode{};
    rendergraph::Node* m_pLabelNodesParent{};

    // Raw pointers into m_pLabelNodesParent's children, one per note in the same
    // order as Track::getNotes(); ownership stays with the parent node.
    std::vector<NoteLabelNode*> m_labelNodes;
    QStringList m_cachedContents;
    float m_cachedDevicePixelRatio{0.f};
    float m_cachedBreadth{0.f};

    DISALLOW_COPY_AND_ASSIGN(WaveformRenderNotes);
};
