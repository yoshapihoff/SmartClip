#include "SettingsManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
}

int SettingsManager::maxItems() const
{
    return m_maxItems;
}

bool SettingsManager::launchAtStartup() const
{
    return m_launchAtStartup;
}

bool SettingsManager::saveHistoryOnExit() const
{
    return m_saveHistoryOnExit;
}

void SettingsManager::setMaxItems(int maxItems)
{
    if (m_maxItems != maxItems) {
        m_maxItems = maxItems;
    }
}

void SettingsManager::setLaunchAtStartup(bool enabled)
{
    if (m_launchAtStartup != enabled) {
        m_launchAtStartup = enabled;
    }
}

void SettingsManager::setSaveHistoryOnExit(bool enabled)
{
    if (m_saveHistoryOnExit != enabled) {
        m_saveHistoryOnExit = enabled;
    }
}

void SettingsManager::loadSettings(const QString &filePath)
{
    const QFileInfo fi(filePath);
    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    QFile f(filePath);
    if (!f.exists()) {
        saveSettings(filePath);
        return;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QRegularExpression re(QLatin1String("^\\s*max_items\\s*:\\s*(\\d+)\\s*$"));
        const QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch()) {
            bool ok = false;
            const int v = m.captured(1).toInt(&ok);
            if (ok && v > 0) {
                m_maxItems = v;
            }
        }

        {
            const QRegularExpression re2(QLatin1String("^\\s*launch_at_startup\\s*:\\s*(true|false)\\s*$"));
            const QRegularExpressionMatch m2 = re2.match(line);
            if (m2.hasMatch()) {
                m_launchAtStartup = (m2.captured(1) == QLatin1String("true"));
            }
        }
        {
            const QRegularExpression re3(QLatin1String("^\\s*save_history_on_exit\\s*:\\s*(true|false)\\s*$"));
            const QRegularExpressionMatch m3 = re3.match(line);
            if (m3.hasMatch()) {
                m_saveHistoryOnExit = (m3.captured(1) == QLatin1String("true"));
            }
        }
    }
}

void SettingsManager::saveSettings(const QString &filePath) const
{
    const QFileInfo fi(filePath);
    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    QTextStream out(&f);
    out << "max_items: " << m_maxItems << "\n";
    out << "launch_at_startup: " << (m_launchAtStartup ? "true" : "false") << "\n";
    out << "save_history_on_exit: " << (m_saveHistoryOnExit ? "true" : "false") << "\n";
}
