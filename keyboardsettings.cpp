#include "displaysettings.h"
#include <QKeySequence>

namespace {
struct Command { const char *id, *label, *sequence; };
const Command commands[] = {
    {"playPause", "Play / pause", "Space"}, {"next", "Next song", "Ctrl+Right"},
    {"previous", "Previous song", "Ctrl+Left"}, {"seekForward", "Seek forward", "Right"},
    {"seekBackward", "Seek backward", "Left"}, {"search", "Search", "Ctrl+F"},
    {"addFiles", "Open audio files", "Ctrl+O"},
    {"undo", "Undo queue or playlist edit", "Ctrl+Z"},
    {"settings", "Settings", "Ctrl+,"}, {"quit", "Quit Musica", "Ctrl+Q"},
    {"back", "Go back", "Alt+Left"}, {"fullscreen", "Fullscreen Now Playing", "F11"}
};
}
void DisplaySettings::setReducedMotion(bool enabled)
{
    if (m_reducedMotion == enabled) return;
    m_reducedMotion = enabled; m_saveTimer.start(); emit motionChanged();
}
QVariantMap DisplaySettings::shortcuts() const
{
    QVariantMap result;
    for (const auto &command : commands) result[command.id] = m_shortcuts.value(command.id, command.sequence);
    if (!m_shortcuts.contains("undo")) {
        for (auto it = result.cbegin(); it != result.cend(); ++it)
            if (it.key() != "undo" && it.value().toString() == "Ctrl+Z") { result["undo"] = QString(); break; }
    }
    return result;
}
QVariantList DisplaySettings::shortcutOptions() const
{
    QVariantList result;
    const auto values = shortcuts();
    for (const auto &command : commands) result.append(QVariantMap{{"id", command.id}, {"label", tr(command.label)},
        {"sequence", values.value(command.id)}, {"defaultSequence", command.sequence}});
    return result;
}
QString DisplaySettings::setShortcut(const QString &id, const QString &sequence)
{
    const auto values = shortcuts();
    if (!values.contains(id)) return tr("Unknown command.");
    const auto keys = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    const auto normalized = keys.toString(QKeySequence::PortableText);
    if (!sequence.isEmpty() && (keys.isEmpty() || keys.count() != 1 || keys[0].key() == Qt::Key_unknown)) return tr("Use one key combination.");
    if (!keys.isEmpty()) {
        const auto key = keys[0].key();
        if (key == Qt::Key_Escape || key == Qt::Key_Tab || key == Qt::Key_Backtab || key == Qt::Key_Backspace)
            return tr("Escape, Tab, and Backspace keep their navigation behavior.");
        if (!(keys[0].keyboardModifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) && key >= Qt::Key_0 && key <= Qt::Key_Z)
            return tr("Add Ctrl, Alt, or Meta to a letter or number so typing keeps working.");
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (it.key() != id && it.value().toString() == normalized) return tr("That shortcut is already assigned to another command.");
    }
    m_shortcuts[id] = normalized; m_saveTimer.start(); emit shortcutsChanged(); return {};
}
QString DisplaySettings::assignShortcut(const QString &id, int key, int modifiers)
{
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) return tr("Press a key together with the modifier.");
    return setShortcut(id, QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key))).toString(QKeySequence::PortableText));
}
void DisplaySettings::resetShortcuts()
{
    m_shortcuts.clear(); m_saveTimer.start(); emit shortcutsChanged();
}
