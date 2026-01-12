#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QStringList>
#include <QVector>

class QAction;
class QTimer;

class SmartClipApp final : public QObject
{
    Q_OBJECT

public:
    explicit SmartClipApp(QObject *parent = nullptr);
    void show();

private slots:
    void updateIcon();
    void onClipboardChanged();
    void pollClipboard();
    void onSettings();
    void onQuit();

private:
    struct HistoryItem {
        QString text;
        int usageCount = 0;
        qint64 addedAtMs = 0;
    };

    void rebuildMenu();
    void addToHistory(const QString &text);
    void trimHistoryToMaxItems();
    void loadSettings();
    void saveSettings() const;
    QString settingsFilePath() const;
    void loadHistory();
    void saveHistory() const;
    QString historyFilePath() const;
    void applyLaunchAtStartup(bool enabled);
    QString launchAgentPlistPath() const;
    static QString formatMenuLabel(const QString &text);

    QSystemTrayIcon trayIcon;
    QMenu trayMenu;

    QVector<HistoryItem> history;
    int maxItems = 20;
    bool launchAtStartup = false;
    bool saveHistoryOnExit = true;
    bool ignoreNextClipboardChange = false;
    QString lastClipboardText;
    bool historyDirty = false;
    bool exitHandled = false;

    QTimer *clipboardPollTimer = nullptr;

    QAction *titleAction = nullptr;
    QAction *settingsAction = nullptr;
    QAction *quitAction = nullptr;
};