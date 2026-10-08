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
class QWidget;
class SettingsManager;
class SettingsDialog;
class HelpDialog;
class HistoryManager;
class LaunchAgentManager;
class MqttClient;
class SyncManager;
class TrayPopup;

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
    /** Периодический тик темы: обновить иконку трея И тему попапа. */
    void onThemeTick();
    void onClipboardChanged();
    void pollClipboard();
    void onSettings();
    void onHelp();
    void onQuit();
    void onClearHistory();
    void onDeleteItem(const QString &text);
    void onToggleFavorite(const QString &text);
    
    // Методы для управления цветами избранного
    int getFavoriteColorIndex(const QString &text);
    void releaseFavoriteColor(const QString &text);

private:
    void rebuildMenu();
    /** Тема рабочего стола: true — тёмная (значит иконку трея надо светлую). */
    static bool desktopPrefersDark();
    /**
     * Тема для ОКОН приложения (отличается от desktopPrefersDark: там
     * решение принимается для иконки на ВЕРХНЕЙ ПАНЕЛИ GNOME, которая
     * всегда тёмная; для окон 'default' ≠ тёмная).
     */
    static bool windowPrefersDark();
    /** Сразу сохранить историю (избранное/маски) — чтобы пережило перезапуск. */
    void persistHistory();
    /** Переключить маскировку пункта (пароль) и сразу сохранить. */
    void toggleItemMask(const QString &text);
    QString settingsFilePath() const;
    QString historyFilePath() const;
    static QString formatMenuLabel(const QString &text);
    void handleClipboardChange();
    void handleExitCleanup();
    /** Сообщить синку о локальном изменении (если синк включён). */
    void notifySync();

    QSystemTrayIcon trayIcon;
    QMenu trayMenu;

    // СВОЁ окно вместо меню десктопа (по умолчанию; отключается
    // SMARTCLIP_NO_TRAY_POPUP=1). Даёт контроль над видом/отступами.
    TrayPopup *trayPopup = nullptr;
    bool trayPopupEnabled = false;
    /** Таймер опроса файла-сигнала «закрыть попап» (клик вне окна). */
    QTimer *popupDismissTimer = nullptr;
    /** Время показа попапа (мс) — чтобы не реагировать на старый сигнал. */
    qint64 popupShownAtMs = 0;
    /** Собрать строки для попапа из истории и показать окно у иконки. */
    void showTrayPopup();
    /** Пересобрать содержимое попапа (если он включён). */
    void refreshTrayPopup();
    /** Применить текущую системную тему к попапу (светлая/тёмная). */
    void updatePopupTheme();

    bool ignoreNextClipboardChange = false;
    QString lastClipboardText;
    bool exitHandled = false;

    QTimer *clipboardPollTimer = nullptr;
    SettingsManager *settingsManager = nullptr;
    HistoryManager *historyManager = nullptr;
    LaunchAgentManager *launchAgentManager = nullptr;
    MqttClient *mqttClient = nullptr;
    SyncManager *syncManager = nullptr;

    QAction *titleAction = nullptr;
    QAction *settingsAction = nullptr;
    QAction *helpAction = nullptr;
    QAction *quitAction = nullptr;
    QAction *clearHistoryAction = nullptr;
    QAction *favoriteModeAction = nullptr;
    QAction *revealModeAction = nullptr;   // «режим вскрытия паролей»
    bool revealMode = false;
    QAction *commentModeAction = nullptr;  // «режим комментариев»
    bool commentMode = false;
    QAction *deleteModeAction = nullptr;   // «режим удаления»
    bool deleteMode = false;
    /** Модальное окно ввода комментария. true — нажали «Окей». */
    bool promptComment(const QString &text, QString &out);
    /** Сбросить все режимы (одноразовое поведение после действия). */
    void clearModes();
    /** Сбросить только режим удаления (не трогая другие режимы). */
    void resetDeleteMode();
    QTimer *iconThemeTimer = nullptr;   // периодическая проверка темы (Linux/Wayland)
    QTimer *historyAutosaveTimer = nullptr;   // авто-сохранение истории/избранного
    bool iconDarkValid = false;         // был ли уже применён цвет иконки
    bool iconDarkApplied = false;       // тёмная ли тема была применена
    // «Режим избранного»: на GNOME Wayland Ctrl+клик недоходит до приложения,
    // поэтому клик по элементу работает как тумблер избранного (см. rebuildMenu).
    bool favoriteMode = false;

#if defined(Q_OS_MAC)
    /**
     * Невидимое окно-хелпер для macOS. Перед показом попапа выводит
     * приложение вперёд: иначе Qt::Popup на macOS показывается и тут же
     * закрывается AppKit'ом (баг меню-барных/status-item приложений).
     * Обход подтверждён в трекере Qt (forum.qt.io/topic/164883).
     */
    void macBringAppForward();
    QWidget *macFocusHelper = nullptr;
#endif
    
    // Цвета для иконок избранного
    static const QColor favoriteColors[33]; // 32 цвета + белый
    QHash<QString, int> favoriteItemColors; // Сохраняем закрепленные цвета за элементами
};