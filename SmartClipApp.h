#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QVector>
#include <QAction>
#include <QHash>
#include <QSet>

class QAction;
class QTimer;
class SettingsManager;
class SettingsDialog;
class HelpDialog;
class HistoryManager;
class LaunchAgentManager;

class SmartClipApp final : public QObject
{
    Q_OBJECT

public:
    explicit SmartClipApp(QObject *parent = nullptr);
    void show();

    /** Masks string for menu: first+last N chars visible, rest '*'. N: len>=10 -> 3, len>=7 -> 2, else 1. Used by tests. */
    static QString maskForMenuDisplay(const QString &text);

private slots:
    void updateIcon();
    void onClipboardChanged();
    void pollClipboard();
    void onSettings();
    void onHelp();
    void onQuit();
    void onClearHistory();
    void onToggleFavorite(const QString &text);
    
    // Методы для управления цветами избранного
    int getFavoriteColorIndex(const QString &text);
    void releaseFavoriteColor(const QString &text);

private:
    void rebuildMenu();
    QString settingsFilePath() const;
    QString historyFilePath() const;
    static QString formatMenuLabel(const QString &text);
    void handleClipboardChange();
    void handleExitCleanup();

    QSystemTrayIcon trayIcon;
    QMenu trayMenu;

    bool ignoreNextClipboardChange = false;
    QString lastClipboardText;
    bool exitHandled = false;

    QTimer *clipboardPollTimer = nullptr;
    SettingsManager *settingsManager = nullptr;
    HistoryManager *historyManager = nullptr;
    LaunchAgentManager *launchAgentManager = nullptr;

    QAction *titleAction = nullptr;
    QAction *settingsAction = nullptr;
    QAction *helpAction = nullptr;
    QAction *quitAction = nullptr;
    QAction *clearHistoryAction = nullptr;
    
    // Цвета для иконок избранного
    static const QColor favoriteColors[33]; // 32 цвета + белый
    QHash<QString, int> favoriteItemColors; // Сохраняем закрепленные цвета за элементами
};