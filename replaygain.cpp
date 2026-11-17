#include "replaygain.h"
#include "taglibpath.h"
#include <fileref.h>
#include <tpropertymap.h>
#include <QRegularExpression>
#include <cmath>

QVariantMap ReplayGain::read(const QString &file)
{
    const auto path = TagLibPath::encode(file);
    TagLib::FileRef ref(TagLibPath::data(path), false);
    if (ref.isNull() || !ref.file()) return {};
    const auto properties = ref.file()->properties();
    QVariantMap result;
    const QRegularExpression number(QStringLiteral("^([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+))\\s*(?:dB)?$"), QRegularExpression::CaseInsensitiveOption);
    for (const auto &kind : {QString("track"), QString("album")}) for (const auto &field : {QString("gain"), QString("peak")}) {
        const auto key = QString("REPLAYGAIN_%1_%2").arg(kind, field).toUpper().toLatin1();
        const auto values = properties.value(TagLib::String(key.constData(), TagLib::String::UTF8));
        if (values.isEmpty()) continue;
        const auto text = QString::fromUtf8(values.front().toCString(true)).trimmed();
        const auto match = number.match(text); if (!match.hasMatch()) continue;
        bool valid = false; const double value = match.captured(1).toDouble(&valid);
        if (!valid || !std::isfinite(value) || (field == "gain" ? value < -60 || value > 30 : value <= 0 || value > 64)) continue;
        result[kind + (field == "gain" ? "Gain" : "Peak")] = value;
    }
    return result;
}

double ReplayGain::scale(const QVariantMap &tags, const QString &mode)
{
    if (mode != "track" && mode != "album") return 1;
    const auto prefix = mode == "album" && tags.contains("albumGain") ? QStringLiteral("album") : QStringLiteral("track");
    bool valid = false; const double gain = tags.value(prefix + "Gain").toDouble(&valid);
    if (!valid || !std::isfinite(gain) || gain < -60 || gain > 30) return 1;
    double scale = std::pow(10.0, gain / 20);
    const double peak = tags.value(prefix + "Peak").toDouble(&valid);
    if (valid && std::isfinite(peak) && peak > 0 && peak <= 64) scale = qMin(scale, 1 / peak);
    return scale;
}
