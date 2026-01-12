#include "LaunchAgentManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QProcess>
#include <QCoreApplication>

#if defined(Q_OS_MAC)
#include <unistd.h>
#endif

LaunchAgentManager::LaunchAgentManager(QObject *parent)
    : QObject(parent)
{
}

void LaunchAgentManager::applyLaunchAtStartup(bool enabled)
{
#if defined(Q_OS_MAC)
    const QString path = plistPath();
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
#else
    (void)enabled;
#endif
}

QString LaunchAgentManager::plistPath() const
{
#if defined(Q_OS_MAC)
    return QDir::homePath() + QLatin1String("/Library/LaunchAgents/com.yoshapihoff.smartclip.plist");
#else
    return QString();
#endif
}
