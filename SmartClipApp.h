#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QVector>
#include <QAction>
#include <QHash>
#include <QSet>
#include <QEvent>
#include <QShortcut>
#include <QTimer>
class SettingsManager;
class SettingsDialog;
class HistoryManager;
class LaunchAgentManager;

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
    void onClearHistory();
    void onToggleFavorite(const QString &text);
    
    // Методы для управления цветами избранного
    int getFavoriteColorIndex(const QString &text);
    void releaseFavoriteColor(const QString &text);
    
    // Методы для маскирования элементов
    void toggleMaskItem(const QString &text);
    QString maskText(const QString &text) const;
    
    // Методы для шифрования
    QByteArray encryptData(const QByteArray &data) const;
    QByteArray decryptData(const QByteArray &data) const;
    QByteArray getEncryptionKey() const;

private:
    void rebuildMenu();
    void loadHistory();
    void saveHistory() const;
    QString settingsFilePath() const;
    QString historyFilePath() const;
    QString launchAgentPlistPath() const;
    static QString formatMenuLabel(const QString &text);

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
    QAction *quitAction = nullptr;
    QAction *clearHistoryAction = nullptr;
    
    // Цвета для иконок избранного
    static const QColor favoriteColors[8]; // 7 цветов + белый
    static int favoriteColorIndex;
    QHash<QString, int> favoriteItemColors; // Сохраняем закрепленные цвета за элементами
    QHash<QString, bool> maskedItems; // Сохраняем элементы которые нужно маскировать
};