#pragma once

#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace FileTags {
constexpr int Version = 1;
// All functions perform local I/O and are called on worker threads.
QVariantMap read(const QString &path, const QString &artworkDirectory, bool fingerprint = false);
QVariantMap write(const QString &path, const QVariantMap &changes, const QString &coverAction,
    const QUrl &coverFile, const QString &expectedRevision, const QString &artworkDirectory);
void applyToTrack(QVariantMap &track, const QVariantMap &tags, bool clearMissing = false);
QString portableKey(int key);
}
