#include "widget/wwaveformviewer.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QEvent>
#include <QMenu>
#include <algorithm>

#include "control/controlproxy.h"
#include "mixer/basetrackplayer.h"
#include "mixer/playermanager.h"
#include "moc_wwaveformviewer.cpp"
#include "track/beats.h"
#include "track/track.h"
#include "util/dnd.h"
#include "util/math.h"
#include "waveform/waveformwidgetfactory.h"
#include "waveform/widgets/waveformwidgetabstract.h"
#include "widget/wcuemenupopup.h"
#include "widget/wglwidget.h"
#include "widget/wnotemenupopup.h"

WWaveformViewer::WWaveformViewer(
        const QString& group,
        UserSettingsPointer pConfig,
        PlayerManager* pPlayerManager,
        QWidget* parent)
        : WWidget(parent),
          m_group(group),
          m_pConfig(pConfig),
          m_zoomZoneWidth(20),
          m_bScratching(false),
          m_bBending(false),
          m_pCueMenuPopup(make_parented<WCueMenuPopup>(pConfig, this)),
          m_pNoteMenuPopup(make_parented<WNoteMenuPopup>(this)),
          m_pPlayerManager(pPlayerManager),
          m_waveformWidget(nullptr) {
    setMouseTracking(true);
    setAcceptDrops(true);
    m_pZoom = new ControlProxy(group, "waveform_zoom", this, ControlFlag::NoAssertIfMissing);
    m_pZoom->connectValueChanged(this, &WWaveformViewer::onZoomChange);

    m_pQuantizeEnabled = new ControlProxy(
            group, "quantize", this, ControlFlag::NoAssertIfMissing);

    connect(m_pNoteMenuPopup.get(),
            &WNoteMenuPopup::noteRemoved,
            this,
            &WWaveformViewer::slotRemoveNote);

    m_pScratchPositionEnable = new ControlProxy(
            group, "scratch_position_enable", this, ControlFlag::NoAssertIfMissing);
    m_pScratchPosition = new ControlProxy(
            group, "scratch_position", this, ControlFlag::NoAssertIfMissing);
    m_pWheel = new ControlProxy(
            group, "wheel", this, ControlFlag::NoAssertIfMissing);
    m_pPlayEnabled = new ControlProxy(group, "play", this, ControlFlag::NoAssertIfMissing);
    m_pPassthroughEnabled = make_parented<ControlProxy>(group, "passthrough", this);
    m_pPassthroughEnabled->connectValueChanged(this, &WWaveformViewer::passthroughChanged);

    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::NoFocus);
}

WWaveformViewer::~WWaveformViewer() {
    //qDebug() << "~WWaveformViewer";
}

void WWaveformViewer::setup(const QDomNode& node, const SkinContext& context) {
    if (m_waveformWidget) {
        m_waveformWidget->setup(node, context);
        m_dimBrightThreshold = m_waveformWidget->getDimBrightThreshold();
    }
}

void WWaveformViewer::resizeEvent(QResizeEvent* event) {
    Q_UNUSED(event);
    if (m_waveformWidget) {
        // Note m_waveformWidget is a WaveformWidgetAbstract,
        // so this calls the method of WaveformWidgetAbstract,
        // note of the derived waveform widgets which are also
        // a QWidget, though that will be called directly.
        m_waveformWidget->resize(width(), height());
    }
}

void WWaveformViewer::showEvent(QShowEvent* event) {
    Q_UNUSED(event);
    if (m_waveformWidget) {
        // We leave it up to Qt to set the size of the derived
        // waveform widget, but we still need to set the size
        // of the renderer.
        m_waveformWidget->resizeRenderer(
                width(), height(), static_cast<float>(devicePixelRatioF()));
    }
}

void WWaveformViewer::mousePressEvent(QMouseEvent* event) {
    if (!m_waveformWidget || m_waveformWidget->getType() == WaveformWidgetType::Empty) {
        return;
    }

    m_mouseAnchor = event->pos();

    if (event->button() == Qt::LeftButton) {
        // Pressing a standing note arms a move/edit gesture instead of scratching
        // (concept section 6): dragging past a threshold moves the note, a click
        // without dragging opens its editor (decided on release). Only in the
        // standing view, where the labels are shown.
        if (!isPlaying()) {
            NotePointer pNote = m_waveformWidget->getNoteLabelAtPoint(event->pos());
            if (pNote) {
                m_pPressedNote = pNote;
                m_bDraggingNote = false;
                setCursor(Qt::ClosedHandCursor);
                return;
            }
        }
        // If we are pitch-bending then disable and reset because the two
        // shouldn't be used at once.
        if (m_bBending) {
            m_pWheel->setParameter(0.5);
            m_bBending = false;
        }
        m_bScratching = true;
        int eventPosValue = m_waveformWidget->getOrientation() == Qt::Horizontal ?
                    event->pos().x() : event->pos().y();
        double audioSamplePerPixel = m_waveformWidget->getAudioSamplePerPixel();
        double targetPosition = -1.0 * eventPosValue * audioSamplePerPixel * 2;
        m_pScratchPosition->set(targetPosition);
        m_pScratchPositionEnable->set(1.0);
    } else if (event->button() == Qt::RightButton) {
        const auto currentTrack = m_waveformWidget->getTrackInfo();
        if (!isPlaying() && m_pHoveredMark) {
            auto cueAtClickPos = getCuePointerFromCueMark(m_pHoveredMark);
            if (cueAtClickPos) {
                m_pCueMenuPopup->setTrackCueGroup(currentTrack, cueAtClickPos, m_group);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                m_pCueMenuPopup->popup(event->globalPosition().toPoint());
#else
                m_pCueMenuPopup->popup(event->globalPos());
#endif
            }
        } else {
            // If we are scratching then disable and reset because the two shouldn't
            // be used at once.
            if (m_bScratching) {
                m_pScratchPositionEnable->set(0.0);
                m_bScratching = false;
            }
            m_pWheel->setParameter(0.5);
            m_bBending = true;
        }
    }

    // Set the cursor to a hand while the mouse is down (when cue menu is not open).
    if (!m_pCueMenuPopup->isVisible()) {
        setCursor(Qt::ClosedHandCursor);
    }
}

void WWaveformViewer::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!m_waveformWidget || m_waveformWidget->getType() == WaveformWidgetType::Empty) {
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    // The single click preceding this double-click already opened the editor for
    // a note under the cursor (a click on a note edits it). Don't act again on
    // top of the open popup.
    if (m_pNoteMenuPopup->isVisible()) {
        return;
    }
    // A double-click is meant for authoring a note, not scratching: undo the
    // scratch the preceding single press started.
    if (m_bScratching) {
        m_pScratchPositionEnable->set(0.0);
        m_bScratching = false;
    }
    // Notes are authored in the standing view; while playing the labels are
    // hidden and replaced by the live-ETA preview.
    if (isPlaying()) {
        return;
    }
    const TrackPointer pTrack = m_waveformWidget->getTrackInfo();
    if (!pTrack) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint globalPos = event->globalPosition().toPoint();
#else
    const QPoint globalPos = event->globalPos();
#endif
    // Double-clicking an existing note edits it rather than stacking a new one
    // on top of it.
    NotePointer pExisting = m_waveformWidget->getNoteLabelAtPoint(event->pos());
    if (pExisting) {
        openNoteEditor(pExisting, false, globalPos);
        return;
    }
    createNoteAt(event->pos(), globalPos);
}

void WWaveformViewer::mouseMoveEvent(QMouseEvent* event) {
    if (!m_waveformWidget || m_waveformWidget->getType() == WaveformWidgetType::Empty) {
        return;
    }

    // Dragging an armed standing note moves it (concept section 6). A small
    // movement is still treated as a click (handled on release); past the
    // threshold the note follows the cursor live.
    if (m_pPressedNote) {
        if (!m_bDraggingNote &&
                (event->pos() - m_mouseAnchor).manhattanLength() >=
                        QApplication::startDragDistance()) {
            m_bDraggingNote = true;
        }
        if (m_bDraggingNote) {
            const mixxx::audio::FramePos position = framePosFromMouse(event->pos());
            if (position.isValid()) {
                // The note is connected to the track, so this marks it dirty and
                // re-renders immediately. framePosFromMouse snaps to the nearest
                // beat when the deck's quantize is on, exactly like authoring.
                m_pPressedNote->setPosition(position);
            }
        }
        return;
    }

    // Only send signals for mouse moving if the left button is pressed
    if (m_bScratching) {
        int eventPosValue = m_waveformWidget->getOrientation() == Qt::Horizontal ?
                    event->pos().x() : event->pos().y();
        // Adjusts for one-to-one movement.
        double audioSamplePerPixel = m_waveformWidget->getAudioSamplePerPixel();
        double targetPosition = -1.0 * eventPosValue * audioSamplePerPixel * 2;
        //qDebug() << "Target:" << targetPosition;
        m_pScratchPosition->set(targetPosition);
    } else if (m_bBending) {
        QPoint diff = event->pos() - m_mouseAnchor;
        int diffValue = m_waveformWidget->getOrientation() == Qt::Horizontal ?
                    diff.x() : diff.y();
        // Start at the middle of [0.0, 1.0], and emit values based on how far
        // the mouse has traveled horizontally. Note, for legacy (MIDI) reasons,
        // this is tuned to 127.
        // NOTE(rryan): This is basically a direct connection to the "wheel"
        // control since we manually connect it in LegacySkinParser regardless
        // of whether the skin specifies it. See ControlTTRotaryBehavior to see
        // where this value is handled.
        double v = 0.5 + (diffValue / 1270.0);
        // clamp to [0.0, 1.0]
        v = math_clamp(v, 0.0, 1.0);
        m_pWheel->setParameter(v);
    } else if (!isPlaying()) {
        // Hint that a standing note's label is clickable to edit it.
        if (m_waveformWidget->getNoteLabelAtPoint(event->pos())) {
            setCursor(Qt::PointingHandCursor);
        } else if (cursor().shape() == Qt::PointingHandCursor) {
            setCursor(Qt::ArrowCursor);
        }
        WaveformMarkPointer pMark;
        pMark = m_waveformWidget->getCueMarkAtPoint(event->pos());
        if (pMark && getCuePointerFromCueMark(pMark)) {
            if (!m_pHoveredMark) {
                m_pHoveredMark = pMark;
                highlightMark(pMark);
            } else if (pMark != m_pHoveredMark) {
                unhighlightMark(m_pHoveredMark);
                m_pHoveredMark = pMark;
                highlightMark(pMark);
            }
        } else {
            if (m_pHoveredMark) {
                unhighlightMark(m_pHoveredMark);
                m_pHoveredMark = nullptr;
            }
        }
    }
}

void WWaveformViewer::mouseReleaseEvent(QMouseEvent* event) {
    const QPoint pressPos = m_mouseAnchor;

    // Finish an armed ETA-note gesture (concept section 6). If the press never
    // became a drag it was a click: open the editor. Otherwise the note was
    // already moved live, so just clean up.
    if (m_pPressedNote) {
        const NotePointer pNote = m_pPressedNote;
        const bool wasDragging = m_bDraggingNote;
        m_pPressedNote.reset();
        m_bDraggingNote = false;
        m_mouseAnchor = QPoint();
        setCursor(Qt::ArrowCursor);
        if (!wasDragging && event && event->button() == Qt::LeftButton) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            openNoteEditor(pNote, false, event->globalPosition().toPoint());
#else
            openNoteEditor(pNote, false, event->globalPos());
#endif
        }
        return;
    }

    if (m_bScratching) {
        m_pScratchPositionEnable->set(0.0);
        m_bScratching = false;
    }
    if (m_bBending) {
        m_pWheel->setParameter(0.5);
        m_bBending = false;
    }
    m_mouseAnchor = QPoint();

    // Set the cursor back to an arrow.
    setCursor(Qt::ArrowCursor);

    // A right-click without dragging (so the pitch-bend above was a no-op) on the
    // standing waveform opens the note context menu (concept section 6: create a
    // note via right-click; existing notes also offer "edit" there).
    if (event && event->button() == Qt::RightButton && m_waveformWidget &&
            m_waveformWidget->getType() != WaveformWidgetType::Empty &&
            !isPlaying() && !m_pHoveredMark && !m_pCueMenuPopup->isVisible() &&
            (event->pos() - pressPos).manhattanLength() <= 2 &&
            m_waveformWidget->getTrackInfo() &&
            m_waveformWidget->getTrackSamples() > 0.0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        showNoteContextMenu(event->pos(), event->globalPosition().toPoint());
#else
        showNoteContextMenu(event->pos(), event->globalPos());
#endif
    }
}

void WWaveformViewer::wheelEvent(QWheelEvent* event) {
    if (m_waveformWidget) {
        if (event->angleDelta().y() > 0) {
            onZoomChange(m_waveformWidget->getZoom() / 1.05);
        } else if (event->angleDelta().y() < 0) {
            onZoomChange(m_waveformWidget->getZoom() * 1.05);
        }
    }
}

void WWaveformViewer::dragEnterEvent(QDragEnterEvent* pEvent) {
    DragAndDropHelper::handleTrackDragEnterEvent(pEvent, m_group, m_pConfig);
}

void WWaveformViewer::dropEvent(QDropEvent* pEvent) {
    DragAndDropHelper::handleTrackDropEvent(pEvent, *this, m_group, m_pConfig);
}

bool WWaveformViewer::handleDragAndDropEventFromWindow(QEvent* pEvent) {
    return event(pEvent);
}

void WWaveformViewer::leaveEvent(QEvent*) {
    if (m_pHoveredMark) {
        unhighlightMark(m_pHoveredMark);
        m_pHoveredMark = nullptr;
    }
}

void WWaveformViewer::slotTrackLoaded(TrackPointer track) {
    if (m_waveformWidget) {
        m_waveformWidget->setTrack(track);
    }
}

#ifdef __STEM__
void WWaveformViewer::slotSelectStem(mixxx::StemChannelSelection stemMask) {
    if (m_waveformWidget) {
        m_waveformWidget->selectStem(stemMask);
        update();
    }
}
#endif

void WWaveformViewer::slotTrackUnloaded(TrackPointer pOldTrack) {
    slotLoadingTrack(pOldTrack, TrackPointer());
}

void WWaveformViewer::slotLoadingTrack(TrackPointer pNewTrack, TrackPointer pOldTrack) {
    Q_UNUSED(pNewTrack);
    Q_UNUSED(pOldTrack);
    if (m_waveformWidget) {
        m_waveformWidget->setTrack(TrackPointer());
    }
}

void WWaveformViewer::onZoomChange(double zoom) {
    //qDebug() << "WaveformWidgetRenderer::onZoomChange" << this << zoom;
    setZoom(zoom);
    // notify back the factory to sync zoom if needed
    WaveformWidgetFactory::instance()->notifyZoomChange(this);
}

void WWaveformViewer::setZoom(double zoom) {
    //qDebug() << "WaveformWidgetRenderer::setZoom" << zoom;
    if (m_waveformWidget) {
        m_waveformWidget->setZoom(zoom);
    }

    // If multiple waveform widgets for the same group are created then it's
    // possible that this setZoom() is coming from another waveform with the
    // same group. That means that if we set the zoom control here, that
    // waveform will receive the update as a call to onZoomChange which will in
    // turn notify the WaveformWidgetFactory that zoom changed which will
    // infinite loop because we will receive another setZoom() from
    // WaveformWidgetFactory. To prevent this recursion, check for no-ops.
    if (m_pZoom->get() != zoom) {
        m_pZoom->set(zoom);
    }
}

void WWaveformViewer::setDisplayBeatGridAlpha(int alpha) {
    if (m_waveformWidget) {
        m_waveformWidget->setDisplayBeatGridAlpha(alpha);
    }
}

void WWaveformViewer::setPlayMarkerPosition(double position) {
    if (m_waveformWidget) {
        m_waveformWidget->setPlayMarkerPosition(position);
    }
}

void WWaveformViewer::setWaveformWidget(WaveformWidgetAbstract* waveformWidget) {
    if (m_waveformWidget) {
        QWidget* pWidget = m_waveformWidget->getWidget();
        disconnect(pWidget);
    }
    m_waveformWidget = waveformWidget;
    if (m_waveformWidget) {
        QWidget* pWidget = m_waveformWidget->getWidget();
        DEBUG_ASSERT(pWidget);
        connect(pWidget,
                &QWidget::destroyed,
                this,
                [this]() {
                    // The pointer must be considered as dangling!
                    m_waveformWidget = nullptr;
                });
        m_waveformWidget->getWidget()->setMouseTracking(true);
#ifdef MIXXX_USE_QOPENGL
        if (m_waveformWidget->getGLWidget()) {
            // The OpenGLWindow used to display the waveform widget interferes with the
            // normal Qt tooltip mechanism and uses it's own mechanism. We set the tooltip
            // of the waveform widget to the tooltip of its parent WWaveformViewer so the
            // OpenGLWindow will display it.
            m_waveformWidget->getGLWidget()->setToolTip(toolTip());

            // Tell the WGLWidget that this is its drag&drop target
            m_waveformWidget->getGLWidget()->setTrackDropTarget(this);
        }
#endif
        // Make connection to show "Passthrough" label on the waveform, except for
        // "Empty" waveform type
        if (m_waveformWidget->getType() == WaveformWidgetType::Empty) {
            return;
        }
        connect(this,
                &WWaveformViewer::passthroughChanged,
                this,
                [this](double value) {
                    m_waveformWidget->setPassThroughEnabled(value > 0);
                });
        // Make sure the label is shown after the waveform type was changed
        emit passthroughChanged(m_pPassthroughEnabled->toBool());
    }
}

CuePointer WWaveformViewer::getCuePointerFromCueMark(WaveformMarkPointer pMark) const {
    if (m_waveformWidget && pMark) {
        return m_waveformWidget->getCuePointerFromIndex(pMark->getHotCue());
    }
    return {};
}

void WWaveformViewer::highlightMark(WaveformMarkPointer pMark) {
    QColor highlightColor = Color::chooseContrastColor(pMark->fillColor(),
            m_dimBrightThreshold);
    pMark->setBaseColor(highlightColor, m_dimBrightThreshold);
}

void WWaveformViewer::unhighlightMark(WaveformMarkPointer pMark) {
    auto pCue = getCuePointerFromCueMark(pMark);
    if (pCue) {
        QColor originalColor = mixxx::RgbColor::toQColor(pCue->getColor());
        pMark->setBaseColor(originalColor, m_dimBrightThreshold);
    }
}

bool WWaveformViewer::isPlaying() const {
    return m_pPlayEnabled->toBool();
}

mixxx::audio::FramePos WWaveformViewer::framePosFromMouse(const QPoint& pos) const {
    if (!m_waveformWidget) {
        return {};
    }
    const double trackSamples = m_waveformWidget->getTrackSamples();
    if (trackSamples <= 0.0) {
        return {};
    }
    const int eventPosValue = m_waveformWidget->getOrientation() == Qt::Horizontal
            ? pos.x()
            : pos.y();
    // Inverse of WaveformWidgetRenderer::transformSamplePositionInRendererWorld:
    // pixel -> engine sample position of the displayed waveform.
    double samplePos = eventPosValue * 2.0 * m_waveformWidget->getAudioSamplePerPixel() +
            m_waveformWidget->getFirstDisplayedPosition() * trackSamples;
    samplePos = std::clamp(samplePos, 0.0, trackSamples);
    auto framePos = mixxx::audio::FramePos::fromEngineSamplePos(samplePos);

    // Snap to the nearest beat when quantize is enabled (concept section 6).
    if (m_pQuantizeEnabled && m_pQuantizeEnabled->toBool()) {
        const TrackPointer pTrack = m_waveformWidget->getTrackInfo();
        if (pTrack) {
            const mixxx::BeatsPointer pBeats = pTrack->getBeats();
            if (pBeats) {
                const auto closest = pBeats->findClosestBeat(framePos);
                if (closest.isValid()) {
                    framePos = closest;
                }
            }
        }
    }
    return framePos;
}

void WWaveformViewer::createNoteAt(const QPoint& widgetPos, const QPoint& globalPos) {
    if (!m_waveformWidget) {
        return;
    }
    const TrackPointer pTrack = m_waveformWidget->getTrackInfo();
    if (!pTrack) {
        return;
    }
    const mixxx::audio::FramePos position = framePosFromMouse(widgetPos);
    if (!position.isValid()) {
        return;
    }
    auto pNote = NotePointer(new Note(position, QString(), TrackId()));
    QList<NotePointer> notes = pTrack->getNotes();
    notes.append(pNote);
    pTrack->setNotes(notes);
    openNoteEditor(pNote, /*isNew=*/true, globalPos);
}

void WWaveformViewer::openNoteEditor(
        const NotePointer& pNote, bool isNew, const QPoint& globalPos) {
    if (!m_waveformWidget || !pNote) {
        return;
    }
    const TrackPointer pTrack = m_waveformWidget->getTrackInfo();
    if (!pTrack) {
        return;
    }

    // Collect the tracks loaded on the *other* decks for the reference dropdown
    // (concept section 8). Decks without a track are skipped.
    QList<WNoteMenuPopup::DeckTrackInfo> otherDeckTracks;
    if (m_pPlayerManager) {
        const int numDecks = m_pPlayerManager->numberOfDecks();
        for (int i = 0; i < numDecks; ++i) {
            const QString group = PlayerManager::groupForDeck(i);
            if (group == m_group) {
                continue;
            }
            BaseTrackPlayer* pPlayer = m_pPlayerManager->getDeckBase(i);
            if (!pPlayer) {
                continue;
            }
            const TrackPointer pDeckTrack = pPlayer->getLoadedTrack();
            if (!pDeckTrack || !pDeckTrack->getId().isValid()) {
                continue;
            }
            QString desc = pDeckTrack->getTitle().trimmed();
            if (desc.isEmpty()) {
                desc = tr("(untitled)");
            }
            const QString artist = pDeckTrack->getArtist().trimmed();
            if (!artist.isEmpty()) {
                desc += QStringLiteral(" - ") + artist;
            }
            WNoteMenuPopup::DeckTrackInfo info;
            info.deckNumber = i + 1;
            info.trackId = pDeckTrack->getId();
            info.label = tr("Deck %1: %2").arg(QString::number(i + 1), desc);
            otherDeckTracks.append(info);
        }
    }

    m_pNoteMenuPopup->setNote(pTrack, pNote, isNew, otherDeckTracks);
    m_pNoteMenuPopup->popup(globalPos);
}

void WWaveformViewer::showNoteContextMenu(
        const QPoint& widgetPos, const QPoint& globalPos) {
    if (!m_waveformWidget) {
        return;
    }
    NotePointer pNote = m_waveformWidget->getNoteLabelAtPoint(widgetPos);
    QMenu menu(this);
    if (pNote) {
        QAction* pEdit = menu.addAction(tr("Edit ETA note"));
        if (menu.exec(globalPos) == pEdit) {
            openNoteEditor(pNote, false, globalPos);
        }
    } else {
        QAction* pAdd = menu.addAction(tr("Add ETA note here"));
        if (menu.exec(globalPos) == pAdd) {
            createNoteAt(widgetPos, globalPos);
        }
    }
}

void WWaveformViewer::slotRemoveNote(NotePointer pNote) {
    if (!m_waveformWidget || !pNote) {
        return;
    }
    const TrackPointer pTrack = m_waveformWidget->getTrackInfo();
    if (!pTrack) {
        return;
    }
    QList<NotePointer> notes = pTrack->getNotes();
    if (notes.removeAll(pNote) > 0) {
        pTrack->setNotes(notes);
    }
}
