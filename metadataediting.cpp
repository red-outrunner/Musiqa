#include "tracklibrary.h"
#include "filetags.h"
#include "albumgrouping.h"

#include <QStandardPaths>
#include <QtConcurrentRun>

namespace {
QString artworkDirectory() { return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/artwork"); }
}

void TrackListModel::initializeMetadataEditing()
{
    connect(&m_metadataWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this] { finishMetadataOperation(false); });
    connect(&m_batchMetadataWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this] { finishMetadataOperation(true); });
}

void TrackListModel::applyMetadataResult(const QString &url, const QVariantMap &result, const QVariantMap &changes)
{
    ensureTracksLoaded();
    const int row = m_trackRows.value(url, -1);
    if (row < 0) return;
    auto track = m_tracks[row].toMap();
    if (track.contains("automaticAlbum")) {
        track["album"] = track.take("automaticAlbum");
        track["albumId"] = track.take("automaticAlbumId");
        track["albumArtist"] = track.take("automaticAlbumArtist");
    }
    track.remove("manualAlbum");
    FileTags::applyToTrack(track, result, true);
    track["modified"] = result.value("modified"); track["size"] = result.value("size");
    track["metadataVersion"] = MetadataCacheVersion; track["probed"] = true; track["metadataError"] = QString();
    if (changes.contains("album") || changes.contains("albumArtist")) {
        m_albumOverrides.remove(url);
        for (auto &entry : m_albumUndo) { auto edits = entry.toMap(); edits.remove(url); entry = edits; }
        track.remove("albumId"); emit albumEditsChanged();
    }
    AlbumGrouping::updateTrackOrder(track, QUrl(url).toLocalFile());
    AlbumGrouping::updateIdentity(track);
    m_tracks[row] = track;
    m_metadataCache[url] = track;
}

void TrackListModel::finishMetadataOperation(bool batch)
{
    if (!m_metadataBusy || batch != m_batchOperation) return;
    const auto result = batch ? m_batchMetadataWatcher.result() : m_metadataWatcher.result();
    const bool saving = m_metadataSaving;
    m_metadataBusy = m_metadataSaving = false;
    if (saving) {
        if (batch) {
            const auto saved = result.value("saved").toMap();
            for (auto it = saved.cbegin(); it != saved.cend(); ++it)
                applyMetadataResult(it.key(), it.value().toMap(), m_metadataChanges);
        } else if (!result.contains("error")) {
            applyMetadataResult(m_metadataUrl, result, m_metadataChanges);
        }
        publishUpdates(); saveSettings();
    }
    emit metadataBusyChanged();
    if (result.contains("error")) emit metadataError(result.value("error").toString());
    else if (batch && saving) emit batchMetadataSaved(result);
    else if (batch) emit batchMetadataLoaded(result);
    else if (saving) emit metadataSaved(m_metadataUrl);
    else {
        auto details = result; details["libraryTrack"] = trackForUrl(m_metadataUrl);
        emit metadataLoaded(details);
    }
    if (saving) resumeMetadataProbes();
}

void TrackListModel::suspendMetadataProbes()
{
    m_resumeIndexing = m_indexing;
    m_probeTimeout.stop(); m_nextProbe.stop(); m_tracksNotification.stop();
    ++m_generation; m_probeIndex = -1; m_finishingProbe = false;
    m_probe.setSource(QUrl()); m_indexing = false; emit scanningChanged();
}

void TrackListModel::resumeMetadataProbes()
{
    if (m_rescanPending) {
        m_rescanPending = false; m_resumeIndexing = false; rescan(); return;
    }
    if (!m_resumeIndexing) return;
    m_resumeIndexing = false; m_indexing = true;
    m_processed = m_failed = 0;
    for (const auto &entry : m_tracks) {
        const auto track = entry.toMap();
        m_processed += track.value("probed").toBool();
        m_failed += !track.value("metadataError").toString().isEmpty();
    }
    m_probeIndex = -1; m_nextProbe.start(); emit scanningChanged();
}

void TrackListModel::loadSongMetadata(const QString &fileUrl)
{
    if (m_metadataBusy) { emit metadataError(tr("Finish the current metadata operation first.")); return; }
    const auto track = trackForUrl(fileUrl);
    if (track.isEmpty()) { emit metadataError(tr("This song is no longer in your library.")); return; }
    m_metadataUrl = track.value("fileUrl").toString();
    m_metadataBusy = true; m_metadataSaving = false; m_batchOperation = false; emit metadataBusyChanged();
    const auto path = QUrl(m_metadataUrl).toLocalFile(), directory = artworkDirectory();
    m_metadataWatcher.setFuture(QtConcurrent::run([path, directory] { return FileTags::read(path, directory, true); }));
}

bool TrackListModel::saveSongMetadata(const QString &fileUrl, const QVariantMap &changes, const QString &coverAction, const QUrl &coverFile, const QString &revision)
{
    if (m_metadataBusy) { emit metadataError(tr("Finish the current metadata operation first.")); return false; }
    if (m_scanning) { emit metadataError(tr("The library is discovering files. Try saving again when discovery finishes.")); return false; }
    const auto track = trackForUrl(fileUrl);
    if (track.isEmpty()) { emit metadataError(tr("This song is no longer in your library.")); return false; }
    m_metadataUrl = track.value("fileUrl").toString(); m_metadataChanges = changes;
    suspendMetadataProbes();
    m_metadataBusy = m_metadataSaving = true; m_batchOperation = false; emit metadataBusyChanged();
    const auto path = QUrl(m_metadataUrl).toLocalFile(), directory = artworkDirectory();
    m_metadataWatcher.setFuture(QtConcurrent::run([path, changes, coverAction, coverFile, revision, directory] {
        return FileTags::write(path, changes, coverAction, coverFile, revision, directory);
    }));
    return true;
}

void TrackListModel::loadBatchMetadata(const QStringList &urls)
{
    if (m_metadataBusy) { emit metadataError(tr("Finish the current metadata operation first.")); return; }
    QStringList unique;
    for (const auto &url : urls) {
        if (trackForUrl(url).isEmpty()) { emit metadataError(tr("A selected song is no longer in your library.")); return; }
        if (!unique.contains(url)) unique.append(url);
    }
    if (unique.isEmpty()) { emit metadataError(tr("Select at least one song.")); return; }
    m_metadataBusy = true; m_metadataSaving = false; m_batchOperation = true; emit metadataBusyChanged();
    const auto directory = artworkDirectory();
    m_batchMetadataWatcher.setFuture(QtConcurrent::run([this, unique, directory] {
        QVariantMap common, mixed, revisions;
        QVariantList errors;
        bool editable = true;
        for (int i = 0; i < unique.size(); ++i) {
            const auto details = FileTags::read(QUrl(unique[i]).toLocalFile(), directory, true);
            if (details.contains("error") || !details.value("writable").toBool()) {
                errors.append(QVariantMap{{"fileUrl", unique[i]}, {"error", details.value("error", QStringLiteral("This song or its folder is read-only."))}});
                editable = false;
            }
            const auto fields = details.value("fields").toMap();
            if (i == 0) common = fields;
            else for (auto it = common.begin(); it != common.end(); ++it)
                if (it.value().toString() != fields.value(it.key()).toString()) mixed[it.key()] = true;
            revisions[unique[i]] = details.value("revision");
            QMetaObject::invokeMethod(this, [this, i, total = unique.size()] { emit batchMetadataProgress(i + 1, total); }, Qt::QueuedConnection);
        }
        for (auto it = mixed.cbegin(); it != mixed.cend(); ++it) common[it.key()] = QString();
        return QVariantMap{{"urls", unique}, {"fields", common}, {"mixed", mixed}, {"revisions", revisions}, {"editable", editable}, {"errors", errors}};
    }));
}

bool TrackListModel::saveBatchMetadata(const QStringList &urls, const QVariantMap &changes, const QString &coverAction, const QUrl &coverFile, const QVariantMap &revisions)
{
    if (m_metadataBusy) { emit metadataError(tr("Finish the current metadata operation first.")); return false; }
    if (m_scanning) { emit metadataError(tr("Wait for file discovery to finish before saving.")); return false; }
    QStringList unique;
    for (const auto &url : urls) {
        if (trackForUrl(url).isEmpty() || revisions.value(url).toString().isEmpty()) {
            emit metadataError(tr("Reopen the batch editor to read all selected songs before saving.")); return false;
        }
        if (!unique.contains(url)) unique.append(url);
    }
    if (unique.isEmpty() || (changes.isEmpty() && coverAction == "keep")) return false;
    suspendMetadataProbes();
    m_metadataBusy = m_metadataSaving = m_batchOperation = true; m_metadataChanges = changes; emit metadataBusyChanged();
    const auto directory = artworkDirectory();
    m_batchMetadataWatcher.setFuture(QtConcurrent::run([this, unique, changes, coverAction, coverFile, revisions, directory] {
        QVariantMap saved;
        QVariantList errors;
        for (int i = 0; i < unique.size(); ++i) {
            const auto result = FileTags::write(QUrl(unique[i]).toLocalFile(), changes, coverAction, coverFile, revisions.value(unique[i]).toString(), directory);
            if (result.contains("error")) errors.append(QVariantMap{{"fileUrl", unique[i]}, {"error", result.value("error")}});
            else saved[unique[i]] = result;
            QMetaObject::invokeMethod(this, [this, i, total = unique.size()] { emit batchMetadataProgress(i + 1, total); }, Qt::QueuedConnection);
        }
        return QVariantMap{{"saved", saved}, {"errors", errors}, {"total", unique.size()}};
    }));
    return true;
}
