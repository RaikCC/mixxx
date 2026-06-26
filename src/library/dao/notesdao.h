#pragma once

#include "library/dao/dao.h"
#include "track/note.h"
#include "track/trackid.h"

#define NOTE_TABLE "track_notes"

class Note;

class NotesDAO : public DAO {
  public:
    ~NotesDAO() override = default;

    QList<NotePointer> getNotesForTrack(TrackId trackId) const;

    void saveTrackNotes(TrackId trackId, const QList<NotePointer>& noteList) const;
    bool deleteNotesForTrack(TrackId trackId) const;
    bool deleteNotesForTracks(const QList<TrackId>& trackIds) const;

  private:
    bool saveNote(TrackId trackId, Note* pNote) const;
};
