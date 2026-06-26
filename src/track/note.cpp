#include "track/note.h"

#include "moc_note.cpp"
#include "util/compatibility/qmutex.h"

//static
void NotePointer::deleteLater(Note* pNote) {
    if (pNote) {
        pNote->deleteLater();
    }
}

Note::Note(
        DbId id,
        mixxx::audio::FramePos position,
        const QString& content,
        TrackId refTrackId)
        : m_bDirty(false), // clear flag after loading from database
          m_dbId(id),
          m_position(position),
          m_content(content),
          m_refTrackId(refTrackId) {
    DEBUG_ASSERT(m_dbId.isValid());
}

Note::Note(
        mixxx::audio::FramePos position,
        const QString& content,
        TrackId refTrackId)
        : m_bDirty(true), // not yet in database, needs to be saved
          m_position(position),
          m_content(content),
          m_refTrackId(refTrackId) {
    DEBUG_ASSERT(!m_dbId.isValid());
}

bool Note::isDirty() const {
    const auto lock = lockMutex(&m_mutex);
    return m_bDirty;
}

void Note::setDirty(bool dirty) {
    const auto lock = lockMutex(&m_mutex);
    m_bDirty = dirty;
}

DbId Note::getId() const {
    const auto lock = lockMutex(&m_mutex);
    return m_dbId;
}

void Note::setId(DbId id) {
    const auto lock = lockMutex(&m_mutex);
    m_dbId = id;
    // Neither mark as dirty nor emit updated(): this is only called after
    // inserting the note into the database; the id is not visible elsewhere.
}

mixxx::audio::FramePos Note::getPosition() const {
    const auto lock = lockMutex(&m_mutex);
    return m_position;
}

void Note::setPosition(mixxx::audio::FramePos position) {
    auto lock = lockMutex(&m_mutex);
    if (m_position == position) {
        return;
    }
    m_position = position;
    m_bDirty = true;
    lock.unlock();
    emit updated();
}

QString Note::getContent() const {
    const auto lock = lockMutex(&m_mutex);
    return m_content;
}

void Note::setContent(const QString& content) {
    auto lock = lockMutex(&m_mutex);
    if (m_content == content) {
        return;
    }
    m_content = content;
    m_bDirty = true;
    lock.unlock();
    emit updated();
}

TrackId Note::getRefTrackId() const {
    const auto lock = lockMutex(&m_mutex);
    return m_refTrackId;
}

void Note::setRefTrackId(TrackId refTrackId) {
    auto lock = lockMutex(&m_mutex);
    if (m_refTrackId == refTrackId) {
        return;
    }
    m_refTrackId = refTrackId;
    m_bDirty = true;
    lock.unlock();
    emit updated();
}
