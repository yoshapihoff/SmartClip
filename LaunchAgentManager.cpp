#include "LaunchAgentManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QCoreApplication>
#include <QStandardPaths>

#if !defined(Q_OS_WIN)
#include <QProcess>
#endif

#if defined(Q_OS_WIN)
 #include <windows.h>
#endif

LaunchAgentManager::LaunchAgentManager(QObject *parent)
    : QObject(parent)
{
}

// ─── Linux: XDG Autostart ────────────────────────────────────────────
#if defined(Q_OS_LINUX)

void LaunchAgentManager::applyLaunchAtStartup(bool enabled)
{
    const QString path = autostartFilePath();
    const QFileInfo fi(path);

    if (!enabled) {
        QFile::remove(path);
        return;
    }

    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    const QString program = QCoreApplication::applicationFilePath();

    QTextStream out(&f);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=SmartClip\n";
    out << "Comment=Clipboard History Manager\n";
    out << "Exec=" << program << "\n";
    out << "Icon=smartclip\n";
    out << "Terminal=false\n";
    out << "X-GNOME-Autostart-enabled=true\n";
    out << "X-KDE-autostart-after=panel\n";
    out << "NoDisplay=true\n";
    out << "Categories=Utility;\n";
}

QString LaunchAgentManager::autostartFilePath() const
{
    return QDir::homePath() + QLatin1String("/.config/autostart/smartclip.desktop");
}

// ─── macOS: launchd plist ────────────────────────────────────────────
#elif defined(Q_OS_MAC)

void LaunchAgentManager::applyLaunchAtStartup(bool enabled)
{
    const QString path = autostartFilePath();
    const QFileInfo fi(path);
    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    if (!enabled) {
        QProcess::execute("/bin/launchctl", {"unload", path});
        QFile::remove(path);
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    const QString program = QCoreApplication::applicationFilePath();

    QTextStream out(&f);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    out << "<plist version=\"1.0\">\n";
    out << "<dict>\n";
    out << "  <key>Label</key>\n";
    out << "  <string>com.yoshapihoff.smartclip</string>\n";
    out << "  <key>ProgramArguments</key>\n";
    out << "  <array>\n";
    out << "    <string>" << program << "</string>\n";
    out << "  </array>\n";
    out << "  <key>RunAtLoad</key>\n";
    out << "  <true/>\n";
    out << "</dict>\n";
    out << "</plist>\n";

    QProcess::execute("/bin/launchctl", {"unload", path});
    QProcess::execute("/bin/launchctl", {"load", path});
}

QString LaunchAgentManager::autostartFilePath() const
{
    return QDir::homePath() + QLatin1String("/Library/LaunchAgents/com.yoshapihoff.smartclip.plist");
}

// ─── Windows: реестр HKCU\...\CurrentVersion\Run ─────────────────────
#elif defined(Q_OS_WIN)

namespace {
// Имя значения в ключе Run (одно на приложение).
constexpr const wchar_t *kRunValueName = L"SmartClip";
constexpr const wchar_t *kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}  // namespace

void LaunchAgentManager::applyLaunchAtStartup(bool enabled)
{
    HKEY hkey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &hkey, nullptr) != ERROR_SUCCESS)
        return;

    if (!enabled) {
        RegDeleteValueW(hkey, kRunValueName);
        RegCloseKey(hkey);
        return;
    }

    // В реестре путь к исполняемому файлу принято брать в кавычки (путь может
    // содержать пробелы). Кодировка значения — UTF-16.
    const QString program =
        QStringLiteral("\"") + QCoreApplication::applicationFilePath()
        + QStringLiteral("\"");
    const DWORD bytes = DWORD((program.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(hkey, kRunValueName, 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(program.utf16()), bytes);
    RegCloseKey(hkey);
}

QString LaunchAgentManager::autostartFilePath() const
{
    // Файла нет — автозапуск живёт в реестре. Возвращаем ключ для тестов
    // (проверка состояния через реестр, см. LaunchAgentManagerTest).
    return QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}

// ─── Other platforms: no-op ──────────────────────────────────────────
#else

void LaunchAgentManager::applyLaunchAtStartup(bool enabled)
{
    (void)enabled;
}

QString LaunchAgentManager::autostartFilePath() const
{
    return QString();
}

#endif
