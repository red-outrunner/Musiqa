#pragma once
#include <QVariantMap>
namespace ReplayGain {
QVariantMap read(const QString &file);
double scale(const QVariantMap &tags, const QString &mode);
}
