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
    QString plistPath() const;
};
