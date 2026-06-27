#pragma once

#include <QComboBox>
#include <QLabel>
#include <QList>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <memory>

#include "track/note.h"
#include "track/track_decl.h"
#include "track/trackid.h"
#include "util/widgethelper.h"

/// Transient popup editor for a single ETA Note (concept document section 6
/// "Bearbeiten direkt auf der Waveform" and section 8 "Track-Referenz").
///
/// Modeled on WCueMenuPopup: a Qt::Popup window that the WWaveformViewer shows
/// at the cursor. Edits are committed live to the Note object (which is already
/// connected to its Track, so the track is marked dirty and the waveform
/// re-renders immediately). Closing the popup by clicking outside therefore just
/// "saves" implicitly; an accidental empty new note is discarded on close.
class WNoteMenuPopup : public QWidget {
    Q_OBJECT
  public:
    explicit WNoteMenuPopup(QWidget* parent = nullptr);

    /// One loaded track on another deck, offered in the reference dropdown.
    struct DeckTrackInfo {
        int deckNumber;   // 1-indexed deck number
        TrackId trackId;  // id of the track loaded there
        QString label;    // e.g. "Deck 2: Title - Artist"
    };

    /// Configure the popup for a note before showing it. `isNew` marks a freshly
    /// created note so an unedited empty one is discarded again on close.
    /// `otherDeckTracks` populates the reference dropdown (concept section 8).
    void setNote(const TrackPointer& pTrack,
            const NotePointer& pNote,
            bool isNew,
            const QList<DeckTrackInfo>& otherDeckTracks);

    void popup(const QPoint& p) {
        auto* pParent = qobject_cast<QWidget*>(parent());
        if (!pParent) {
            return;
        }
        QPoint topLeft = mixxx::widgethelper::mapPopupToScreen(*pParent, p, size());
        move(topLeft);
        show();
    }

    void show() {
        m_pEditContent->setFocus();
        QWidget::show();
    }

  signals:
    /// The note should be removed from its track (user pressed delete, or a new
    /// note was left empty and is being discarded).
    void noteRemoved(NotePointer pNote);

  private slots:
    void slotContentChanged();
    void slotReferenceChanged(int index);
    void slotDeleteClicked();

  private:
    void resetDeleteConfirm();

    TrackPointer m_pTrack;
    NotePointer m_pNote;
    bool m_isNew{false};
    bool m_deleteArmed{false};
    bool m_deleted{false};

    // Reference target per combo item, aligned with the combo's rows. Index 0 is
    // always the invalid TrackId ("own track"); the rest mirror the dropdown.
    QList<TrackId> m_refTrackIds;

    std::unique_ptr<QLabel> m_pPositionLabel;
    std::unique_ptr<QPlainTextEdit> m_pEditContent;
    std::unique_ptr<QComboBox> m_pRefCombo;
    std::unique_ptr<QPushButton> m_pDeleteButton;

  protected:
    void closeEvent(QCloseEvent* event) override;
};
