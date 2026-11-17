#pragma once

#include <QHash>
#include <QVariantList>

namespace AlbumGrouping {
void updateIdentity(QVariantMap &track);
QVariantList artistCredits(const QString &artist, bool split, const QString &separators);
void updateTrackOrder(QVariantMap &track, const QString &filePath);
QHash<QString, QString> reconcile(QVariantList &tracks);
bool trackLessThan(const QVariant &left, const QVariant &right);
}
