#pragma once

#include <QColor>
#include <QImage>
#include <QStringList>
#include <memory>
#include <vector>

#include "rendergraph/node.h"
#include "track/track.h"
#include "util/class.h"
#include "waveform/renderers/waveformrendererabstract.h"

class QDomNode;
class SkinContext;
class ControlProxy;

namespace rendergraph {
class GeometryNode;
} // namespace rendergraph

namespace allshader {
class WaveformRenderNotes;
class NoteLabelNode;
class DigitsRenderNode;
} // namespace allshader

/// Renders the track's ETA Notes (concept document sections 6 and 7) on the
/// waveform. There are two display modes, switched on the deck's play state:
///
///  - Standing view (section 6, track not playing): a vertical marker line at
///    each note position plus a baked text-texture label showing the note
///    content, both anchored at the note's timecode (they scroll with the
///    waveform).
///  - Live-ETA view (section 7, track playing): the marker lines keep scrolling,
///    but the note labels are replaced by a single preview anchored at the play
///    position, showing a countdown (in beats and/or time) to the next upcoming
///    note followed by its content. This is the headline feature ("ETA").
///
/// Modeled on allshader::WaveformRenderMark: marker lines like WaveformRenderBeat,
/// label textures like the mark nodes, and the countdown reuses the same
/// until-mark mechanics (updateUntilMark / DigitsRenderNode).
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

    // Creates the per-deck control proxies (play state, remaining time). Called
    // by WaveformWidgetRenderer::init() once the group is known.
    bool init() override;

    // Called from WaveformWidget::paintGL with a current OpenGL context.
    void update();

  public slots:
    void setColor(const QColor& color) {
        m_color = color;
    }
    // Live-ETA options (concept section 7). For now driven by these slots /
    // defaults; the preferences UI is wired up in the later settings phase (2d).
    void setEtaShowBeats(bool show) {
        m_etaShowBeats = show;
    }
    void setEtaShowTime(bool show) {
        m_etaShowTime = show;
    }
    // false: the note's left edge sits at the play marker (note reaches into the
    //        future/right side, on top of the upcoming waveform) -- concept default.
    // true:  the note's right edge sits at the play marker (note sits in the
    //        "past"/left side, keeping the upcoming waveform readable).
    void setEtaAlignRightEdgeAtPlayhead(bool alignRight) {
        m_etaAlignRightEdgeAtPlayhead = alignRight;
    }

  private:
    QImage bakeLabel(const QString& content, float devicePixelRatio) const;
    // Bakes the live-ETA bar: one continuous rounded box holding an empty
    // countdown field (width fieldWidth, on the left, where the live digits are
    // drawn on top) followed by the note content text.
    QImage bakeEtaBar(const QString& content,
            float fieldWidth,
            float devicePixelRatio) const;
    void rebuildLabels(const QList<NotePointer>& notes, float devicePixelRatio);

    // Computes the beats and time from the play position to the next upcoming
    // note. Modeled on WaveformRenderMark::updateUntilMark.
    void updateUntilNote(double playPosition, double nextNotePosition);

    QColor m_color;

    rendergraph::GeometryNode* m_pLinesNode{};
    rendergraph::Node* m_pLabelNodesParent{};
    rendergraph::Node* m_pEtaBarNodesParent{};
    DigitsRenderNode* m_pDigitsNode{};

    // Raw pointers into m_pLabelNodesParent's children, one per note in the same
    // order as Track::getNotes(); ownership stays with the parent node.
    std::vector<NoteLabelNode*> m_labelNodes;
    QStringList m_cachedContents;
    float m_cachedDevicePixelRatio{0.f};
    float m_cachedBreadth{0.f};

    // The live-ETA bar (background box + content text); created lazily (needs a
    // GL context) and re-baked only when its content, field width, dpr or color
    // changes. The live countdown digits are drawn on top of it every frame.
    NoteLabelNode* m_pEtaBarNode{};
    QString m_cachedEtaBarContent;
    float m_cachedEtaBarFieldWidth{-1.f};
    float m_cachedEtaBarDevicePixelRatio{0.f};
    QColor m_cachedEtaBarColor;

    // Live-ETA state and options.
    int m_beatsUntilNote{0};
    double m_timeUntilNote{0.0};
    std::unique_ptr<ControlProxy> m_pPlayControl;
    std::unique_ptr<ControlProxy> m_pTimeRemainingControl;
    bool m_etaShowBeats{true};
    bool m_etaShowTime{true};
    bool m_etaAlignRightEdgeAtPlayhead{false};

    DISALLOW_COPY_AND_ASSIGN(WaveformRenderNotes);
};
