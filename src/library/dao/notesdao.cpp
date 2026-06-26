#include "library/dao/notesdao.h"

#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QtDebug>

#include "library/queryutil.h"
#include "util/assert.h"
#include "util/db/fwdsqlquery.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger = mixxx::Logger("NotesDAO");

/// Read the optional `ref_track_id` without triggering DbId's "invalid
/// identifier" critical log: a NULL (or non-positive) value means the note
/// refers to its own track and yields an invalid TrackId.
TrackId refTrackIdFromQVariant(const QVariant& value) {
    if (value.isNull()) {
        return TrackId();
    }
    bool ok = false;
    const int id = value.toInt(&ok);
    if (!ok || id < 0) {
        return TrackId();
    }
    return TrackId(value);
}

NotePointer noteFromRow(const QSqlRecord& row) {
    const auto id = DbId(row.value(row.indexOf("id")));
    const auto position =
            mixxx::audio::FramePos::fromEngineSamplePosMaybeInvalid(
                    row.value(row.indexOf("position")).toDouble());
    const QString content = row.value(row.indexOf("content")).toString();
    const TrackId refTrackId =
            refTrackIdFromQVariant(row.value(row.indexOf("ref_track_id")));
    NotePointer pNote(new Note(id, position, content, refTrackId));
    return pNote;
}

} // namespace

QList<NotePointer> NotesDAO::getNotesForTrack(TrackId trackId) const {
    QList<NotePointer> notes;

    FwdSqlQuery query(
            m_database,
            QStringLiteral("SELECT * FROM " NOTE_TABLE " WHERE track_id=:id"));
    DEBUG_ASSERT(
            query.isPrepared() &&
            !query.hasError());
    query.bindValue(":id", trackId);
    if (!query.execPrepared()) {
        kLogger.warning()
                << "Failed to load notes of track"
                << trackId;
        DEBUG_ASSERT(!"failed query");
        return notes;
    }
    while (query.next()) {
        NotePointer pNote = noteFromRow(query.record());
        if (!pNote) {
            continue;
        }
        notes.push_back(pNote);
    }
    return notes;
}

bool NotesDAO::deleteNotesForTrack(TrackId trackId) const {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM " NOTE_TABLE " WHERE track_id=:track_id"));
    query.bindValue(":track_id", trackId.toVariant());
    if (query.exec()) {
        return true;
    } else {
        LOG_FAILED_QUERY(query);
    }
    return false;
}

bool NotesDAO::deleteNotesForTracks(const QList<TrackId>& trackIds) const {
    QStringList idList;
    for (const auto& trackId : trackIds) {
        idList << trackId.toString();
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM " NOTE_TABLE " WHERE track_id in (%1)")
                          .arg(idList.join(",")));
    if (query.exec()) {
        return true;
    } else {
        LOG_FAILED_QUERY(query);
    }
    return false;
}

bool NotesDAO::saveNote(TrackId trackId, Note* pNote) const {
    VERIFY_OR_DEBUG_ASSERT(pNote) {
        return false;
    }

    // Prepare query
    QSqlQuery query(m_database);
    if (pNote->getId().isValid()) {
        // Update note
        query.prepare(QStringLiteral("UPDATE " NOTE_TABLE " SET "
                                     "track_id=:track_id,"
                                     "position=:position,"
                                     "content=:content,"
                                     "ref_track_id=:ref_track_id"
                                     " WHERE id=:id"));
        query.bindValue(":id", pNote->getId().toVariant());
    } else {
        // New note
        query.prepare(QStringLiteral("INSERT INTO " NOTE_TABLE
                                     " (track_id, position, content, ref_track_id)"
                                     " VALUES (:track_id, :position, :content, :ref_track_id)"));
    }

    // Bind values and execute query
    query.bindValue(":track_id", trackId.toVariant());
    query.bindValue(":position", pNote->getPosition().toEngineSamplePosMaybeInvalid());
    query.bindValue(":content", pNote->getContent());
    const TrackId refTrackId = pNote->getRefTrackId();
    // Store an invalid reference as SQL NULL (= "refers to its own track"),
    // not as -1, which DbId::toVariant() would otherwise produce.
    query.bindValue(":ref_track_id",
            refTrackId.isValid() ? refTrackId.toVariant() : QVariant());
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }

    if (!pNote->getId().isValid()) {
        // New note
        const auto newId = DbId(query.lastInsertId());
        DEBUG_ASSERT(newId.isValid());
        pNote->setId(newId);
    }
    DEBUG_ASSERT(pNote->getId().isValid());
    pNote->setDirty(false);
    return true;
}

void NotesDAO::saveTrackNotes(
        TrackId trackId,
        const QList<NotePointer>& noteList) const {
    DEBUG_ASSERT(trackId.isValid());
    QStringList noteIds;
    noteIds.reserve(noteList.size());
    for (const auto& pNote : noteList) {
        // New notes (without an id) must always be marked as dirty
        DEBUG_ASSERT(pNote->getId().isValid() || pNote->isDirty());
        // Update or save note
        if (pNote->isDirty()) {
            saveNote(trackId, pNote.get());
        }
        // After saving each note must have a valid id
        VERIFY_OR_DEBUG_ASSERT(pNote->getId().isValid()) {
            continue;
        }
        noteIds.append(pNote->getId().toString());
    }

    // Delete orphaned notes
    FwdSqlQuery query(
            m_database,
            QStringLiteral("DELETE FROM " NOTE_TABLE " WHERE track_id=:track_id AND id NOT IN (%1)")
                    .arg(noteIds.join(QChar(','))));
    DEBUG_ASSERT(
            query.isPrepared() &&
            !query.hasError());
    query.bindValue(":track_id", trackId);
    if (!query.execPrepared()) {
        kLogger.warning()
                << "Failed to delete orphaned notes of track"
                << trackId;
        DEBUG_ASSERT(!"failed query");
        return;
    }
    if (query.numRowsAffected() > 0) {
        kLogger.debug()
                << "Deleted"
                << query.numRowsAffected()
                << "orphaned note(s) of track"
                << trackId;
    }
}
