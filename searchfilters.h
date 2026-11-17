#pragma once
#include <QVariantMap>
namespace SearchFilters {
QVariantMap normalized(const QVariantMap &input, bool *valid = nullptr);
bool matches(const QVariantMap &track, const QVariantMap &filters);
QString sql(const QVariantMap &filters, QVariantList *values);
}
