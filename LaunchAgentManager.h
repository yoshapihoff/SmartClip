#pragma once

#include <QObject>
#include <QString>

class TestLaunchAgentManager;

class LaunchAgentManager final : public QObject
{
    Q_OBJECT

public:
    explicit LaunchAgentManager(QObject *parent = nullptr);
    ~LaunchAgentManager() = default;

    void applyLaunchAtStartup(bool enabled);

private:
    // Тест проверяет платформенный путь автозапуска (на Windows — ключ
    // реестра, не файл), поэтому нужен доступ к приватному методу.
    friend class TestLaunchAgentManager;

    /** Returns platform-specific autostart file path:
     *   macOS:   ~/Library/LaunchAgents/com.yoshapihoff.smartclip.plist
     *   Linux:   ~/.config/autostart/smartclip.desktop
     *   Windows: HKCU\Software\Microsoft\Windows\CurrentVersion\Run (не файл)
     *   Other:   empty string
     */
    QString autostartFilePath() const;
};
