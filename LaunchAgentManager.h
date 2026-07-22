#pragma once

#include <QObject>
#include <QString>

class LaunchAgentManager final : public QObject
{
    Q_OBJECT

public:
    explicit LaunchAgentManager(QObject *parent = nullptr);
    ~LaunchAgentManager() = default;

    void applyLaunchAtStartup(bool enabled);

private:
    /** Returns platform-specific autostart file path:
     *   macOS:   ~/Library/LaunchAgents/com.yoshapihoff.smartclip.plist
     *   Linux:   ~/.config/autostart/smartclip.desktop
     *   Other:   empty string
     */
    QString autostartFilePath() const;
};
