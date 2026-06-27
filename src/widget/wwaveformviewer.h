#pragma once

#include "audio/frame.h"
#include "track/note.h"
#include "track/track_decl.h"
#include "util/parented_ptr.h"
#include "waveform/renderers/waveformmark.h"
#include "widget/trackdroptarget.h"
#include "widget/wwidget.h"

class ControlProxy;
class PlayerManager;
class WaveformWidgetAbstract;
class WCueMenuPopup;
class WNoteMenuPopup;
class QDomNode;
class SkinContext;

class WWaveformViewer : public WWidget, public TrackDropTarget {
    Q_OBJECT
  public:
    WWaveformViewer(
            const QString& group,
            UserSettingsPointer pConfig,
            PlayerManager* pPlayerManager = nullptr,
            QWidget* parent = nullptr);
    ~WWaveformViewer() override;

    const QString& getGroup() const {
        return m_group;
    }
    void setup(const QDomNode& node, const SkinContext& context);

    bool handleDragAndDropEventFromWindow(QEvent* pEvent) override;

    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    void mousePressEvent(QMouseEvent * /*unused*/) override;
    void mouseDoubleClickEvent(QMouseEvent* /*unused*/) override;
    void mouseMoveEvent(QMouseEvent * /*unused*/) override;
    void mouseReleaseEvent(QMouseEvent * /*unused*/) override;
    void leaveEvent(QEvent* /*unused*/) override;

  signals:
    void trackDropped(const QString& filename, const QString& group) override;
    void cloneDeck(const QString& sourceGroup, const QString& targetGroup) override;
    void passthroughChanged(double value);

  public slots:
    void slotTrackLoaded(TrackPointer track);
    void slotTrackUnloaded(TrackPointer pOldTrack);
    void slotLoadingTrack(TrackPointer pNewTrack, TrackPointer pOldTrack);
#ifdef __STEM__
    void slotSelectStem(mixxx::StemChannelSelection stemMask);
#endif

  protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private slots:
    void onZoomChange(double zoom);
    // The note editor asks us to remove a note (user deleted it, or discarded a
    // new empty one); we rewrite the track's note list.
    void slotRemoveNote(NotePointer pNote);

  private:
    void setWaveformWidget(WaveformWidgetAbstract* waveformWidget);
    WaveformWidgetAbstract* getWaveformWidget() {
        return m_waveformWidget;
    }
    //direct access to let factory sync/set default zoom
    void setZoom(double zoom);
    void setDisplayBeatGridAlpha(int alpha);
    void setPlayMarkerPosition(double position);

  private:
    const QString m_group;
    UserSettingsPointer m_pConfig;
    int m_zoomZoneWidth;
    ControlProxy* m_pZoom;
    ControlProxy* m_pScratchPositionEnable;
    ControlProxy* m_pScratchPosition;
    ControlProxy* m_pWheel;
    ControlProxy* m_pPlayEnabled;
    parented_ptr<ControlProxy> m_pPassthroughEnabled;
    bool m_bScratching;
    bool m_bBending;
    QPoint m_mouseAnchor;
    parented_ptr<WCueMenuPopup> m_pCueMenuPopup;
    parented_ptr<WNoteMenuPopup> m_pNoteMenuPopup;
    PlayerManager* m_pPlayerManager;
    ControlProxy* m_pQuantizeEnabled;
    WaveformMarkPointer m_pHoveredMark;

    // ETA Notes drag-to-move (phase 2c-v2). A left press on a standing note's
    // label arms this gesture: dragging past a threshold moves the note (live,
    // quantized like authoring); a click without dragging opens the editor on
    // release. Null while no note interaction is in progress.
    NotePointer m_pPressedNote;
    bool m_bDraggingNote{false};

    WaveformWidgetAbstract* m_waveformWidget;

    int m_dimBrightThreshold;

    friend class WaveformWidgetFactory;

    CuePointer getCuePointerFromCueMark(WaveformMarkPointer pMark) const;
    void highlightMark(WaveformMarkPointer pMark);
    void unhighlightMark(WaveformMarkPointer pMark);
    bool isPlaying() const;

    // ETA Notes editing (phase 2c). Maps a mouse position to a track position
    // (quantized to the nearest beat when the deck's quantize is on), creates a
    // note there and opens the editor, or opens the editor for an existing note.
    mixxx::audio::FramePos framePosFromMouse(const QPoint& pos) const;
    void createNoteAt(const QPoint& widgetPos, const QPoint& globalPos);
    void openNoteEditor(const NotePointer& pNote, bool isNew, const QPoint& globalPos);
    void showNoteContextMenu(const QPoint& widgetPos, const QPoint& globalPos);
};
