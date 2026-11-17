#pragma once

#include <QFile>
#include <QString>
#include <string>

// TagLib expects UTF-16 native paths on Windows; passing QFile::encodeName()
// there loses characters outside the active Windows code page. Keep the
// owning value alive while its c_str()/constData() is used by a TagLib file.
namespace TagLibPath {
#ifdef Q_OS_WIN
using NativePath = std::wstring;
inline NativePath encode(const QString &path) { return path.toStdWString(); }
inline const wchar_t *data(const NativePath &path) { return path.c_str(); }
#else
using NativePath = QByteArray;
inline NativePath encode(const QString &path) { return QFile::encodeName(path); }
inline const char *data(const NativePath &path) { return path.constData(); }
#endif
}
