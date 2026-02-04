#pragma once

#include <QObject>
#include <QString>

class SettingsManager final : public QObject
{
    Q_OBJECT

signals:
    void settingsChanged();

public:
    explicit SettingsManager(QObject *parent = nullptr);
    ~SettingsManager() = default;

    int maxItems() const;
    bool launchAtStartup() const;
    bool saveHistoryOnExit() const;

    void setMaxItems(int maxItems);
    void setLaunchAtStartup(bool enabled);
    void setSaveHistoryOnExit(bool enabled);

    void loadSettings(const QString &filePath);
    void saveSettings(const QString &filePath) const;
    void saveCurrentSettings();

private:
    int m_maxItems = 20;
    bool m_launchAtStartup = false;
    bool m_saveHistoryOnExit = true;
    QString m_currentFilePath;
};
