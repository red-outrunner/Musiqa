#include "tracklibrary.h"
#include "filetags.h"
#include "albumgrouping.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QtConcurrentRun>

namespace {
QString artworkDirectory() { return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/artwork"); }
}

void TrackListModel::initializeMetadataEditing()
{
    connect(&m_metadataWatcher, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const auto result = m_metadataWatcher.result();
        const bool saving = m_metadataSaving;
        m_metadataBusy = m_metadataSaving = false;
        if (result.contains("error")) {
            emit metadataBusyChanged(); emit metadataError(result.value("error").toString());
            if (saving) rescan();
            return;
        }
        if (!saving) {
            auto details = result;
            details["libraryTrack"] = trackForUrl(m_metadataUrl);
            emit metadataBusyChanged(); emit metadataLoaded(details);
            return;
        }
        const int row = m_trackRows.value(m_metadataUrl, -1);
        if (row >= 0) {
            auto track = m_tracks[row].toMap();
            for (const auto &key : {"automaticAlbum", "automaticAlbumId", "automaticAlbumArtist", "manualAlbum"}) track.remove(key);
            FileTags::applyToTrack(track, result, true);
            track["modified"] = result.value("modified"); track["size"] = result.value("size");
            track["metadataVersion"] = MetadataCacheVersion; track["probed"] = true; track["metadataError"] = QString();
            if (m_metadataChanges.contains("album") || m_metadataChanges.contains("albumArtist")) {
                m_albumOverrides.remove(m_metadataUrl);
                for (auto &entry : m_albumUndo) { auto edits = entry.toMap(); edits.remove(m_metadataUrl); entry = edits; }
                track.remove("albumId"); emit albumEditsChanged();
            }
            AlbumGrouping::updateTrackOrder(track, QUrl(m_metadataUrl).toLocalFile());
            AlbumGrouping::updateIdentity(track);
            m_tracks[row] = track;
            m_metadataCache[m_metadataUrl] = track;
            // The audio samples haven't changed; retain analysis without redecoding.
            if (m_analysisCache.contains(m_metadataUrl) || m_metadataChanges.contains("bpm") || m_metadataChanges.contains("initialKey") || m_metadataChanges.contains("beatOffset")) {
                auto analysis = m_analysisCache.value(m_metadataUrl).toMap();
                analysis["modified"] = result.value("modified"); analysis["size"] = result.value("size");
                const auto fields = result.value("fields").toMap();
                analysis["version"] = MusicAnalysis::Version;
                if (m_metadataChanges.contains("bpm")) {
                    analysis["bpm"] = fields.value("bpm").toString().toDouble();
                    analysis["bpmConfidence"] = track.contains("tagBeatOffset") && analysis.value("bpm").toDouble() > 0 ? 1.0 : 0.0;
                    analysis["manual"] = true;
                }
                if (m_metadataChanges.contains("initialKey")) {
                    analysis["keyId"] = MusicAnalysis::parseKey(fields.value("initialKey").toString());
                    analysis["keyConfidence"] = analysis.value("keyId").toInt() >= 0 ? 1.0 : 0.0;
                    analysis["manual"] = true;
                }
                if (m_metadataChanges.contains("beatOffset")) {
                    analysis["beatOffset"] = fields.value("beatOffset").toString().toDouble();
                    analysis["bpmConfidence"] = track.contains("tagBeatOffset") ? 1.0 : 0.0;
                    analysis["manual"] = true;
                }
                m_analysisCache[m_metadataUrl] = analysis;
            }
            publishUpdates(); saveSettings();
        }
        emit metadataBusyChanged(); emit metadataSaved(m_metadataUrl);
        // Coalesce any refresh requested while tags were being written.
        m_rescanPending = false;
        rescan();
    });
}

void TrackListModel::loadSongMetadata(const QString &fileUrl)
{
    if (m_metadataBusy) { emit metadataError(tr("Finish the current metadata operation first.")); return; }
    const auto track = trackForUrl(fileUrl);
    if (track.isEmpty()) { emit metadataError(tr("This song is no longer in your library.")); return; }
    m_metadataUrl = track.value("fileUrl").toString();
    m_metadataBusy = true; m_metadataSaving = false; emit metadataBusyChanged();
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
    // Discard any in-flight tag/artwork probe for the old file revision.
    m_probeTimeout.stop(); m_nextProbe.stop(); m_tracksNotification.stop();
    ++m_generation; m_probeIndex = -1; m_finishingProbe = false; m_probe.setSource(QUrl());
    m_metadataBusy = m_metadataSaving = true; emit metadataBusyChanged();
    const auto path = QUrl(m_metadataUrl).toLocalFile(), directory = artworkDirectory();
    m_metadataWatcher.setFuture(QtConcurrent::run([path, changes, coverAction, coverFile, revision, directory] {
        return FileTags::write(path, changes, coverAction, coverFile, revision, directory);
    }));
    return true;
}
