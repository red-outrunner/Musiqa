#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

// A small shared rule representation keeps SQLite and the in-memory fallback
// identical. Rules always combine with AND and never contain executable SQL.
namespace CollectionRules {
QString folded(const QString &value);
int releaseYear(const QVariantMap &track);
QVariantMap normalize(const QVariantMap &rules, QString *error = nullptr);
bool matches(const QVariantMap &track, const QVariantMap &rules);
QString sql(const QVariantMap &rules, QVariantList *values);
QString description(const QVariantMap &rules);
bool inFolder(const QString &folder, const QString &root);
}
