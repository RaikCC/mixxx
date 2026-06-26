#pragma once

#include <QMutex>
#include <QObject>
#include <QString>
#include <memory>

#include "audio/frame.h"
#include "track/trackid.h"
#include "util/db/dbid.h"

/// A free-form, timecode-anchored text note attached to a track's waveform
/// (the "ETA Notes" feature, see the concept document section 4).
///
/// A note carries only its content, its position in the track and an optional
/// reference to *another* track (for transition notes, concept section 8).
/// Colors are intentionally NOT stored per note: they are global per-deck
/// settings that are applied at render time (concept section 9). Markdown
/// formatting is part of the content string and a rendering concern.
class Note : public QObject {
    Q_OBJECT

  public:
    Note() = delete;

    /// Load an existing note from the database.
    Note(
            DbId id,
            mixxx::audio::FramePos position,
            const QString& content,
            TrackId refTrackId);

    /// Initialize a new note that has not been saved yet.
    Note(
            mixxx::audio::FramePos position,
            const QString& content,
            TrackId refTrackId);

    ~Note() override = default;

    bool isDirty() const;
    DbId getId() const;

    mixxx::audio::FramePos getPosition() const;
    void setPosition(mixxx::audio::FramePos position);

    QString getContent() const;
    void setContent(const QString& content);

    /// Optional reference to *another* track this note relates to. An invalid
    /// TrackId means the note refers to its own track (the default).
    TrackId getRefTrackId() const;
    void setRefTrackId(TrackId refTrackId);

  signals:
    void updated();

  private:
    void setDirty(bool dirty);
    void setId(DbId id);

    mutable QMutex m_mutex;

    bool m_bDirty;
    DbId m_dbId;
    mixxx::audio::FramePos m_position;
    QString m_content;
    TrackId m_refTrackId;

    friend class Track;
    friend class NotesDAO;
};

class NotePointer : public std::shared_ptr<Note> {
  public:
    NotePointer() = default;
    explicit NotePointer(Note* pNote)
            : std::shared_ptr<Note>(pNote, deleteLater) {
    }

  private:
    static void deleteLater(Note* pNote);
};
