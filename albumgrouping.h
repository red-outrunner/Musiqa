#pragma once

#include <QHash>
#include <QVariantList>

namespace AlbumGrouping {
void updateIdentity(QVariantMap &track);
void updateTrackOrder(QVariantMap &track, const QString &filePath);
QHash<QString, QString> reconcile(QVariantList &tracks);
bool trackLessThan(const QVariant &left, const QVariant &right);
}
