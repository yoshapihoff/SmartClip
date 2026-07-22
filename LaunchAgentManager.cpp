#include "LaunchAgentManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QProcess>
#include <QCoreApplication>
#include <QStandardPaths>

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
