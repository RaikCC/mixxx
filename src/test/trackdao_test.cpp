#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test/librarytest.h"
#include "track/globaltrackcache.h"
#include "track/note.h"
#include "track/track.h"

using ::testing::UnorderedElementsAre;

class TrackDAOTest : public LibraryTest {
};


TEST_F(TrackDAOTest, detectMovedTracks) {
    TrackDAO& trackDAO = internalCollection()->getTrackDAO();

    QString filename = QStringLiteral("file.mp3");

    mixxx::FileInfo oldFile(QDir(QDir::tempPath() + QStringLiteral("/old/dir1")), filename);
    mixxx::FileInfo newFile(QDir(QDir::tempPath() + QStringLiteral("/new/dir1")), filename);
    mixxx::FileInfo otherFile(QDir(QDir::tempPath() + QStringLiteral("/new")), filename);

    TrackPointer pOldTrack = Track::newTemporary(mixxx::FileAccess(oldFile));
    TrackPointer pNewTrack = Track::newTemporary(mixxx::FileAccess(newFile));
    TrackPointer pOtherTrack = Track::newTemporary(mixxx::FileAccess(otherFile));

    // Arbitrary duration
    pOldTrack->setDuration(135);
    pNewTrack->setDuration(135.7);
    pOtherTrack->setDuration(135.7);

    TrackId oldId = internalCollection()->addTrack(pOldTrack, false);
    TrackId newId = internalCollection()->addTrack(pNewTrack, false);
    internalCollection()->addTrack(pOtherTrack, false);

    // Mark as missing
    QSqlQuery query(dbConnection());
    query.prepare("UPDATE track_locations SET fs_deleted=1 WHERE location=:location");
    query.bindValue(":location", oldFile.location());
    query.exec();

    QList<RelocatedTrack> relocatedTracks;
    QStringList addedTracks(newFile.location());
    bool cancel = false;
    trackDAO.detectMovedTracks(&relocatedTracks, addedTracks, &cancel);

    QSet<TrackId> updatedTrackIds;
    QSet<TrackId> removedTrackIds;
    for (const auto& relocatedTrack : std::as_const(relocatedTracks)) {
        updatedTrackIds.insert(relocatedTrack.updatedTrackRef().getId());
        removedTrackIds.insert(relocatedTrack.deletedTrackId());
    }

    EXPECT_THAT(updatedTrackIds, UnorderedElementsAre(oldId));
    EXPECT_THAT(removedTrackIds, UnorderedElementsAre(newId));

    QSet<QString> trackLocations = trackDAO.getAllTrackLocations();
    EXPECT_THAT(trackLocations, UnorderedElementsAre(newFile.location(), otherFile.location()));
}

// Regression test for the bug where a BPM-locked track without a beatgrid
// loses its lock when reloaded from the database.
// https://github.com/mixxxdj/mixxx/issues/15196
TEST_F(TrackDAOTest, bpmLockPreservedForTrackWithoutBeats) {
    const mixxx::FileInfo fileInfo(
            QDir(QDir::tempPath()), QStringLiteral("bpmlocked-no-beats.mp3"));
    TrackPointer pTrack = Track::newTemporary(mixxx::FileAccess(fileInfo));
    pTrack->setDuration(135);
    // Lock the BPM although the track has no beatgrid at all.
    pTrack->setBpmLocked(true);
    ASSERT_FALSE(pTrack->getBeats());
    ASSERT_TRUE(pTrack->isBpmLocked());

    const TrackId trackId = internalCollection()->addTrack(pTrack, false);
    ASSERT_TRUE(trackId.isValid());

    // Dropping the last reference evicts the track from the cache
    // synchronously (eviction runs as a direct call on this thread), so the
    // lookup below reloads it from the database instead of returning the
    // cached in-memory object whose lock flag was never lost.
    pTrack.reset();
    ASSERT_TRUE(GlobalTrackCacheLocker().isEmpty());

    const TrackPointer pReloaded = internalCollection()->getTrackById(trackId);
    ASSERT_TRUE(pReloaded);
    EXPECT_FALSE(pReloaded->getBeats());
    EXPECT_TRUE(pReloaded->isBpmLocked());
}

// Round-trip test for the ETA Notes data layer (concept document section 4):
// notes attached to a track must be persisted to the database when the track
// is saved and restored when it is reloaded.
TEST_F(TrackDAOTest, etaNotesPersistedAcrossReload) {
    const mixxx::FileInfo fileInfo(
            QDir(QDir::tempPath()), QStringLiteral("track-with-eta-notes.mp3"));
    TrackPointer pTrack = Track::newTemporary(mixxx::FileAccess(fileInfo));
    pTrack->setDuration(135);

    const auto pos1 = mixxx::audio::FramePos(1000);
    const auto pos2 = mixxx::audio::FramePos(2000);
    QList<NotePointer> notes;
    notes.append(NotePointer(
            new Note(pos1, QStringLiteral("intro: 8 bars"), TrackId())));
    notes.append(NotePointer(
            new Note(pos2, QStringLiteral("drop"), TrackId())));
    pTrack->setNotes(notes);

    const TrackId trackId = internalCollection()->addTrack(pTrack, false);
    ASSERT_TRUE(trackId.isValid());

    // Dropping the last reference evicts the track from the cache synchronously,
    // so the lookup below reloads it from the database instead of returning the
    // cached in-memory object.
    pTrack.reset();
    ASSERT_TRUE(GlobalTrackCacheLocker().isEmpty());

    const TrackPointer pReloaded = internalCollection()->getTrackById(trackId);
    ASSERT_TRUE(pReloaded);
    const QList<NotePointer> reloadedNotes = pReloaded->getNotes();
    ASSERT_EQ(2, reloadedNotes.size());
    // Notes are returned ordered by position (see NotesDAO::getNotesForTrack).
    EXPECT_EQ(pos1, reloadedNotes.at(0)->getPosition());
    EXPECT_EQ(QStringLiteral("intro: 8 bars"), reloadedNotes.at(0)->getContent());
    EXPECT_EQ(pos2, reloadedNotes.at(1)->getPosition());
    EXPECT_EQ(QStringLiteral("drop"), reloadedNotes.at(1)->getContent());
    // No reference track set -> the note refers to its own track.
    EXPECT_FALSE(reloadedNotes.at(0)->getRefTrackId().isValid());
    // Persisted notes are no longer dirty after the reload.
    EXPECT_FALSE(reloadedNotes.at(0)->isDirty());
}
