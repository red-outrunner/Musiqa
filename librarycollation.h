#pragma once

#include <QCollator>
#include <QLocale>

inline QCollator libraryCollator()
{
    QCollator collator;
    // The C locale ignores numeric collation, including in official Qt builds.
    // Keep the user's collation everywhere else and use natural English order
    // for C/POSIX desktops and headless build machines.
    if (collator.locale().language() == QLocale::C)
        collator.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    return collator;
}
