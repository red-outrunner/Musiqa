#pragma once

#include <QMap>
#include <QVariantList>

namespace GenreGrouping {
QString nameFor(const QString &tag);
QString idFor(const QString &tag);

// Accumulates collection metadata without retaining the songs themselves.
class Builder
{
public:
    void add(const QVariantMap &track);
    QVariantList entries() const;
private:
    QMap<QString, QVariantMap> m_groups;
};
}
