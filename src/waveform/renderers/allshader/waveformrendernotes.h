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
class Context;
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
    // Live-ETA preview window length, in beats (concept section 7
    // "Vorschaufenster", e.g. 64). Only notes arriving within this many beats are
    // shown in the live view; <= 0 means no limit. Same unit as the countdown.
    void setEtaWindowBeats(int beats) {
        m_etaWindowBeats = beats;
    }
    // How long, in beats, a note keeps lingering (dimmed) after it has passed the
    // play marker before it is hidden (concept section 7 "Nachleuchten"). <= 0
    // disables the afterglow.
    void setEtaAfterglowBeats(int beats) {
        m_etaAfterglowBeats = beats;
    }
    // Opacity (0..1) of a note while it lingers after passing the play marker
    // (concept section 7 "Nachleuchten"). Kept fairly opaque so the text stays
    // readable; the preferences UI in phase 2d will drive this.
    void setEtaAfterglowOpacity(float opacity) {
        m_etaAfterglowOpacity = opacity;
    }

  private:
    QImage bakeLabel(const QString& content, float devicePixelRatio) const;
    // Bakes the live-ETA bar: one continuous rounded box holding an empty
    // countdown field (width fieldWidth, on the left, where the live digits are
    // drawn on top) followed by the note content text. opacity (0..1) dims the
    // whole bar, e.g. for the afterglow of a note that has already passed.
    QImage bakeEtaBar(const QString& content,
            float fieldWidth,
            float opacity,
            float devicePixelRatio) const;
    void rebuildLabels(const QList<NotePointer>& notes, float devicePixelRatio);

    // Computes the (signed) beats and seconds from the play position to a note
    // position. Positive = upcoming, <= 0 = already passed. *hasBeats is false
    // when the track has no beat grid (then *beats is 0). Modeled on
    // WaveformRenderMark::updateUntilMark.
    void computeBeatsAndTime(double playPosition,
            double notePosition,
            bool* hasBeats,
            int* beats,
            double* timeSec) const;

    QColor m_color;

    rendergraph::GeometryNode* m_pLinesNode{};
    rendergraph::Node* m_pLabelNodesParent{};
    rendergraph::Node* m_pEtaBarNodesParent{};
    rendergraph::Node* m_pEtaDigitsParent{};

    // Raw pointers into m_pLabelNodesParent's children, one per note in the same
    // order as Track::getNotes(); ownership stays with the parent node.
    std::vector<NoteLabelNode*> m_labelNodes;
    QStringList m_cachedContents;
    float m_cachedDevicePixelRatio{0.f};
    float m_cachedBreadth{0.f};

    // One live-ETA bar (background box + content text) per displayed note, plus a
    // matching countdown-digits node, stacked vertically at the play marker. The
    // pools grow on demand -- creating a texture needs a current GL context, so
    // the nodes are created in update(); unused slots are hidden, never destroyed.
    // A bar is re-baked only when its content, field width, opacity, dpr or color
    // changes; the digits are repositioned every frame.
    struct EtaBarSlot {
        NoteLabelNode* pNode{};
        QString content;
        float fieldWidth{-1.f};
        float opacity{-1.f};
        float devicePixelRatio{0.f};
        QColor color;
    };
    std::vector<EtaBarSlot> m_etaBarSlots;
    std::vector<DigitsRenderNode*> m_etaDigitNodes;

    NoteLabelNode* ensureEtaBarNode(int index,
            rendergraph::Context* pContext,
            const QString& content,
            float fieldWidth,
            float opacity,
            float devicePixelRatio);
    DigitsRenderNode* ensureEtaDigitNode(int index);

    // Live-ETA state and options.
    std::unique_ptr<ControlProxy> m_pPlayControl;
    std::unique_ptr<ControlProxy> m_pTimeRemainingControl;
    bool m_etaShowBeats{true};
    bool m_etaShowTime{true};
    bool m_etaAlignRightEdgeAtPlayhead{false};
    int m_etaWindowBeats{64};
    int m_etaAfterglowBeats{4};
    float m_etaAfterglowOpacity{0.6f};

    DISALLOW_COPY_AND_ASSIGN(WaveformRenderNotes);
};
