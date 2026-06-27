#include "widget/wnotemenupopup.h"

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "moc_wnotemenupopup.cpp"
#include "track/track.h"
#include "util/duration.h"

WNoteMenuPopup::WNoteMenuPopup(QWidget* parent)
        : QWidget(parent) {
    QWidget::hide();
    setWindowFlags(Qt::Popup);
    setAttribute(Qt::WA_StyledBackground);
    setObjectName("WNoteMenuPopup");

    m_pPositionLabel = std::make_unique<QLabel>(this);
    m_pPositionLabel->setToolTip(tr("Note position"));
    m_pPositionLabel->setObjectName("NotePositionLabel");
    m_pPositionLabel->setAlignment(Qt::AlignLeft);

    m_pEditContent = std::make_unique<QPlainTextEdit>(this);
    m_pEditContent->setToolTip(tr("Edit note text (multiple lines allowed)"));
    m_pEditContent->setObjectName("NoteContentEdit");
    m_pEditContent->setPlaceholderText(tr("Note..."));
    m_pEditContent->setTabChangesFocus(true);
    connect(m_pEditContent.get(),
            &QPlainTextEdit::textChanged,
            this,
            &WNoteMenuPopup::slotContentChanged);

    m_pRefCombo = std::make_unique<QComboBox>(this);
    m_pRefCombo->setToolTip(
            tr("Relate this note to a track loaded on another deck"));
    m_pRefCombo->setObjectName("NoteReferenceCombo");
    connect(m_pRefCombo.get(),
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &WNoteMenuPopup::slotReferenceChanged);

    m_pDeleteButton = std::make_unique<QPushButton>(this);
    m_pDeleteButton->setObjectName("NoteDeleteButton");
    connect(m_pDeleteButton.get(),
            &QPushButton::clicked,
            this,
            &WNoteMenuPopup::slotDeleteClicked);

    QVBoxLayout* pMainLayout = new QVBoxLayout();
    pMainLayout->addWidget(m_pPositionLabel.get());
    pMainLayout->addWidget(m_pEditContent.get());
    pMainLayout->addWidget(m_pRefCombo.get());
    pMainLayout->addWidget(m_pDeleteButton.get());
    setLayout(pMainLayout);
    // The size is used to position the popup, so make sure it is computed now.
    layout()->update();
    layout()->activate();
}

void WNoteMenuPopup::setNote(const TrackPointer& pTrack,
        const NotePointer& pNote,
        bool isNew,
        const QList<DeckTrackInfo>& otherDeckTracks) {
    m_pTrack = pTrack;
    m_pNote = pNote;
    m_isNew = isNew;
    m_deleted = false;
    resetDeleteConfirm();

    if (!pTrack || !pNote) {
        return;
    }

    // Block our own change signals while we populate the widgets, so the initial
    // values are not written straight back into the note.
    const QSignalBlocker contentBlocker(m_pEditContent.get());
    const QSignalBlocker comboBlocker(m_pRefCombo.get());

    const mixxx::audio::FramePos position = pNote->getPosition();
    if (position.isValid() && pTrack->getSampleRate().isValid()) {
        const double seconds = position.value() / pTrack->getSampleRate();
        m_pPositionLabel->setText(mixxx::Duration::formatTime(
                seconds, mixxx::Duration::Precision::CENTISECONDS));
    } else {
        m_pPositionLabel->setText(QString());
    }

    m_pEditContent->setPlainText(pNote->getContent());

    // Build the reference dropdown (concept section 8): first the default "own
    // track" entry, then every other deck that currently has a track loaded.
    m_pRefCombo->clear();
    m_refTrackIds.clear();
    m_pRefCombo->addItem(tr("Relate to this track"));
    m_refTrackIds.append(TrackId()); // invalid = own track (default)

    const TrackId refTrackId = pNote->getRefTrackId();
    int selectedIndex = 0;
    for (const auto& deck : otherDeckTracks) {
        m_pRefCombo->addItem(deck.label);
        m_refTrackIds.append(deck.trackId);
        if (refTrackId.isValid() && deck.trackId == refTrackId) {
            selectedIndex = m_pRefCombo->count() - 1;
        }
    }
    // Preserve an existing reference whose track is not loaded on any deck right
    // now, so saving does not silently drop it.
    if (refTrackId.isValid() && selectedIndex == 0) {
        m_pRefCombo->addItem(
                tr("Referenced track (not loaded): #%1").arg(refTrackId.toString()));
        m_refTrackIds.append(refTrackId);
        selectedIndex = m_pRefCombo->count() - 1;
    }
    m_pRefCombo->setCurrentIndex(selectedIndex);
}

void WNoteMenuPopup::slotContentChanged() {
    if (!m_pNote) {
        return;
    }
    m_pNote->setContent(m_pEditContent->toPlainText());
}

void WNoteMenuPopup::slotReferenceChanged(int index) {
    if (!m_pNote || index < 0 || index >= m_refTrackIds.size()) {
        return;
    }
    m_pNote->setRefTrackId(m_refTrackIds.at(index));
}

void WNoteMenuPopup::resetDeleteConfirm() {
    m_deleteArmed = false;
    if (m_pDeleteButton) {
        m_pDeleteButton->setText(tr("Delete note"));
    }
}

void WNoteMenuPopup::slotDeleteClicked() {
    if (!m_pNote) {
        return;
    }
    // Double confirmation (concept section 6): the first click arms the button,
    // the second one actually deletes.
    if (!m_deleteArmed) {
        m_deleteArmed = true;
        m_pDeleteButton->setText(tr("Really delete?"));
        return;
    }
    m_deleted = true;
    NotePointer pNote = m_pNote;
    emit noteRemoved(pNote);
    hide();
}

void WNoteMenuPopup::closeEvent(QCloseEvent* event) {
    // Discard a brand-new note that was never given any content (e.g. a stray
    // double-click), so it does not litter the track. Existing notes may be
    // cleared to empty on purpose, so they are kept.
    if (!m_deleted && m_isNew && m_pNote &&
            m_pNote->getContent().trimmed().isEmpty()) {
        NotePointer pNote = m_pNote;
        emit noteRemoved(pNote);
    }
    QWidget::closeEvent(event);
}
