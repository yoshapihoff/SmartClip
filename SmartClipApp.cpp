#include "SmartClipApp.h"
#include "SettingsManager.h"
#include "SettingsDialog.h"
#include "HelpDialog.h"
#include "HistoryManager.h"
#include "LaunchAgentManager.h"
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QDebug>
#include <QMessageBox>
#include <QTextStream>
#include <QTimer>
#include <QPixmap>
#include <QPainter>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
 #include <QStyleHints>
#endif

// Инициализация статических полей
const QColor SmartClipApp::favoriteColors[33] = {
    QColor(255, 0, 0),     // 0: красный
    QColor(255, 128, 0),   // 1: оранжевый
    QColor(255, 200, 0),   // 2: золотой
    QColor(200, 200, 0),   // 3: жёлтый
    QColor(128, 255, 0),   // 4: лайм
    QColor(0, 200, 0),     // 5: зелёный
    QColor(0, 128, 0),     // 6: тёмно-зелёный
    QColor(0, 255, 128),   // 7: мятный
    QColor(0, 255, 200),   // 8: циан
    QColor(0, 180, 180),   // 9: бирюзовый
    QColor(0, 128, 255),   // 10: небесно-голубой
    QColor(0, 0, 255),     // 11: синий
    QColor(50, 0, 200),    // 12: индиго
    QColor(120, 0, 255),   // 13: фиолетовый
    QColor(160, 0, 200),   // 14: пурпурный
    QColor(255, 0, 200),   // 15: малиновый
    QColor(255, 0, 100),   // 16: тёмно-розовый
    QColor(255, 80, 80),   // 17: коралловый
    QColor(255, 120, 100), // 18: лососевый
    QColor(255, 180, 140), // 19: персиковый
    QColor(200, 160, 100), // 20: бежевый
    QColor(128, 128, 0),   // 21: оливковый
    QColor(30, 140, 30),   // 22: лесной зелёный
    QColor(50, 200, 50),   // 23: лаймово-зелёный
    QColor(0, 255, 80),    // 24: весенне-зелёный
    QColor(0, 200, 160),   // 25: морская волна
    QColor(70, 130, 180),  // 26: стальной синий
    QColor(60, 80, 220),   // 27: королевский синий
    QColor(140, 80, 200),  // 28: средне-фиолетовый
    QColor(200, 80, 180),  // 29: орхидея
    QColor(255, 60, 140),  // 30: ярко-розовый
    QColor(255, 60, 60),   // 31: томатный
    QColor(255, 255, 255)  // 32: белый (для 33+ элементов)
};
SmartClipApp::SmartClipApp(QObject *parent)
    : QObject(parent)
    , settingsManager(new SettingsManager(this))
    , historyManager(new HistoryManager(this))
    , launchAgentManager(new LaunchAgentManager(this))
{
    // Load settings
    settingsManager->loadSettings(settingsFilePath());
    
    // Connect to settings changes for auto-save
    connect(settingsManager, &SettingsManager::settingsChanged, this, [this]() {
        // Settings are automatically saved by SettingsManager now
    });
    
    // Apply launch at startup setting
    launchAgentManager->applyLaunchAtStartup(settingsManager->launchAtStartup());
    
    if (settingsManager->saveHistoryOnExit()) {
        historyManager->loadHistory(historyFilePath(), settingsManager->maxItems());
        // Восстанавливаем закреплённые цвета избранного из загруженной истории
        favoriteItemColors.clear();
        for (const auto &item : historyManager->history()) {
            if (item.favoriteColorIndex >= 0) {
                favoriteItemColors[item.text] = item.favoriteColorIndex;
            }
        }
    } else {
        historyManager->setMaxItems(settingsManager->maxItems());
        QFile::remove(historyFilePath());
    }
    updateIcon();

    titleAction = new QAction("Select the clip you want to add to your clipboard", this);
    titleAction->setEnabled(false);
    
    settingsAction = new QAction("Settings", this);
    connect(settingsAction, &QAction::triggered, this, &SmartClipApp::onSettings);

    clearHistoryAction = new QAction("Clear", this);
    connect(clearHistoryAction, &QAction::triggered, this, &SmartClipApp::onClearHistory);

    helpAction = new QAction("Help", this);
    connect(helpAction, &QAction::triggered, this, &SmartClipApp::onHelp);

    quitAction = new QAction("Quit", this);
    connect(quitAction, &QAction::triggered, this, &SmartClipApp::onQuit);

    rebuildMenu();

    trayIcon.setContextMenu(&trayMenu);
    trayIcon.setToolTip("SmartClip");
    
    // Обработчик правого клика для переключения избранного
    connect(&trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Context) {
            // Правый клик - показываем контекстное меню
            return;
        } else if (reason == QSystemTrayIcon::Trigger) {
            // Левый клик - можно добавить быстрое действие
            return;
        }
    });

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        handleExitCleanup();
    });

    if (QClipboard *clipboard = QApplication::clipboard()) {
        connect(clipboard, &QClipboard::dataChanged, this, &SmartClipApp::onClipboardChanged);
        connect(clipboard, &QClipboard::changed, this, [this](QClipboard::Mode mode) {
            if (mode == QClipboard::Clipboard) {
                onClipboardChanged();
            }
        });
    }

#if defined(Q_OS_MAC)
    clipboardPollTimer = new QTimer(this);
    clipboardPollTimer->setInterval(500);
    connect(clipboardPollTimer, &QTimer::timeout, this, &SmartClipApp::pollClipboard);
    clipboardPollTimer->start();
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QStyleHints *hints = qApp->styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, &SmartClipApp::updateIcon);
    }
#endif
}

void SmartClipApp::show()
{
    trayIcon.show();
}

void SmartClipApp::handleClipboardChange()
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return;
    }

    const QString text = clipboard->text(QClipboard::Clipboard);
    qDebug() << "Clipboard changed: " << text;
    
    if (ignoreNextClipboardChange) {
        ignoreNextClipboardChange = false;
        lastClipboardText = text;
        return;
    }

    if (text.trimmed().isEmpty()) {
        lastClipboardText = text;
        return;
    }

    if (text == lastClipboardText) {
        return;
    }
    lastClipboardText = text;
    
    historyManager->addToHistory(text);
    rebuildMenu();
}

void SmartClipApp::onClipboardChanged()
{
    handleClipboardChange();
}

void SmartClipApp::pollClipboard()
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return;
    }

    const QString currentText = clipboard->text(QClipboard::Clipboard);
    
    // Проверяем, изменился ли текст буфера обмена
    if (currentText != lastClipboardText && !currentText.trimmed().isEmpty()) {
        handleClipboardChange();
    }
}

void SmartClipApp::onHelp()
{
    HelpDialog dialog;
    dialog.exec();
}

void SmartClipApp::onSettings()
{
    SettingsDialog dialog(settingsManager);
    if (dialog.exec() == QDialog::Accepted) {
        // Settings are automatically saved by SettingsManager when changed
        // Apply launch at startup if changed
        launchAgentManager->applyLaunchAtStartup(settingsManager->launchAtStartup());
        
        // Trim history if max items changed
        historyManager->setMaxItems(settingsManager->maxItems());
        
        // Rebuild menu to reflect any changes
        rebuildMenu();
    }
}

void SmartClipApp::handleExitCleanup()
{
    if (exitHandled) {
        return;
    }
    exitHandled = true;

    if (settingsManager->saveHistoryOnExit()) {
        if (historyManager->isDirty()) {
            historyManager->saveHistory(historyFilePath());
        }
    } else {
        QFile::remove(historyFilePath());
    }
}

void SmartClipApp::onQuit()
{
    handleExitCleanup();
    qApp->quit();
}

void SmartClipApp::onClearHistory()
{
    historyManager->clearHistory();
    if (historyManager->history().isEmpty()) {
        favoriteItemColors.clear();
    }
    rebuildMenu();
}

void SmartClipApp::onToggleFavorite(const QString &text)
{
    historyManager->toggleFavorite(text);
    
    // Если элемент добавляется в избранное, закрепляем за ним цвет
    if (historyManager->isFavorite(text)) {
        if (!favoriteItemColors.contains(text)) {
            favoriteItemColors[text] = getFavoriteColorIndex(text);
        }
        historyManager->setFavoriteColor(text, favoriteItemColors[text]);
    } else {
        // Если элемент удаляется из избранного, освобождаем цвет
        releaseFavoriteColor(text);
        historyManager->setFavoriteColor(text, -1);
    }
    
    rebuildMenu();
}

int SmartClipApp::getFavoriteColorIndex(const QString &text)
{
    // Если цвет уже закреплен за этим элементом, возвращаем его
    if (favoriteItemColors.contains(text)) {
        return favoriteItemColors[text];
    }
    
    // Ищем свободный цвет (кроме белого)
    QSet<int> usedColors;
    for (auto it = favoriteItemColors.begin(); it != favoriteItemColors.end(); ++it) {
        if (it.value() < 32) { // Игнорируем белый цвет
            usedColors.insert(it.value());
        }
    }
    
    // Находим первый свободный цвет
    for (int i = 0; i < 32; ++i) {
        if (!usedColors.contains(i)) {
            return i;
        }
    }
    
    // Если все цвета заняты, возвращаем белый (индекс 32)
    return 32;
}

void SmartClipApp::releaseFavoriteColor(const QString &text)
{
    favoriteItemColors.remove(text);
}

void SmartClipApp::rebuildMenu()
{
    trayMenu.clear();

    if (titleAction) {
        trayMenu.addAction(titleAction);
    }

    const auto &history = historyManager->history();
    if (!history.isEmpty()) {
        trayMenu.addSeparator();
    }

    for (int i = 0; i < history.size(); ++i) {
        const QString text = history.at(i).text;
        const bool maskThis = history.at(i).maskInMenu;
        const QString labelText = maskThis ? maskForMenuDisplay(text) : text;
        QAction *action = trayMenu.addAction(formatMenuLabel(labelText));

        // Показываем иконку избранного если элемент в избранном
        if (history.at(i).favoriteColorIndex != -1) {
            action->setIcon(QIcon());
            action->setIconVisibleInMenu(true);
            QPixmap pixmap(12, 12);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            
            // Получаем закрепленный цвет за этим элементом
            // Приоритет: кеш favoriteItemColors → авторитетный favoriteColorIndex из HistoryItem → fallback на белый
            int colorIndex;
            if (favoriteItemColors.contains(text)) {
                colorIndex = favoriteItemColors[text];
                qDebug() << "Color from cache: index" << colorIndex << "for item:" << text;
            } else {
                colorIndex = history.at(i).favoriteColorIndex;
                qDebug() << "No cache entry for" << text << "- using history favoriteColorIndex:" << colorIndex;
                if (colorIndex < 0 || colorIndex > 32) {
                    colorIndex = 32; // fallback на белый
                    qDebug() << "Invalid favoriteColorIndex, falling back to white (32)";
                }
            }
            painter.setBrush(favoriteColors[colorIndex]);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(2, 2, 8, 8);
            painter.end();
            action->setIcon(QIcon(pixmap));
        }
        
        connect(action, &QAction::triggered, this, [this, text]() {
            const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
            if (mods & Qt::ShiftModifier) {
                // Ctrl+Shift+Click — переключить зашифрованное отображение в меню
                historyManager->setMaskInMenu(text, !historyManager->maskInMenu(text));
                rebuildMenu();
            } else if (mods & Qt::ControlModifier) {
                onToggleFavorite(text);
            } else {
                // Обычное копирование в буфер
                historyManager->incrementUsageCount(text);

                if (QClipboard *clipboard = QApplication::clipboard()) {
                    ignoreNextClipboardChange = true;
                    clipboard->setText(text, QClipboard::Clipboard);
                }

                rebuildMenu();
            }
        });
        
        // Добавляем контекстное меню для правого клика
        action->setData(text); // Сохраняем текст для использования в контекстном меню
    }

    trayMenu.addSeparator();

    if (clearHistoryAction) {
        trayMenu.addAction(clearHistoryAction);
    }

    trayMenu.addSeparator();

    if (settingsAction) {
        trayMenu.addAction(settingsAction);
    }
    
    if (helpAction) {
        trayMenu.addAction(helpAction);
    }
    
    if (quitAction) {
        trayMenu.addAction(quitAction);
    }
}

QString SmartClipApp::settingsFilePath() const
{
    return QDir::homePath() + QLatin1String("/.smartclip/settings.yml");
}

QString SmartClipApp::historyFilePath() const
{
    return QDir::homePath() + QLatin1String("/.smartclip/history.yml");
}

QString SmartClipApp::formatMenuLabel(const QString &text)
{
    QString s = text;
    s.replace('\n', ' ');
    s.replace('\r', ' ');
    s = s.simplified();

    const int maxLen = 60;
    if (s.length() > maxLen) {
        s = s.left(maxLen - 3) + "...";
    }
    return s;
}

QString SmartClipApp::maskForMenuDisplay(const QString &text)
{
    const int len = text.length();
    if (len == 0) {
        return text;
    }
    int head = 1, tail = 1;
    if (len >= 10) {
        head = 3;
        tail = 3;
    } else if (len >= 7) {
        head = 2;
        tail = 2;
    }
    if (len <= head + tail) {
        return text;
    }
    const int mid = len - head - tail;
    return text.left(head) + QString(mid, QChar('*')) + text.right(tail);
}

void SmartClipApp::updateIcon()
{
#if defined(Q_OS_MAC)
    // На macOS правильный подход для иконки в menu bar — "template" изображение.
    // Система сама окрашивает его в белый/черный в зависимости от фона панели.
    QIcon icon(":/icons/tray_black.svg");
    icon.setIsMask(true);
    Q_ASSERT(!icon.isNull());
    trayIcon.setIcon(icon);
    return;
#else
    bool darkMode = false;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QStyleHints *hints = qApp->styleHints()) {
        darkMode = (hints->colorScheme() == Qt::ColorScheme::Dark);
    }
#endif

    QIcon icon = darkMode
        ? QIcon(":/icons/tray_white.svg")
        : QIcon(":/icons/tray_black.svg");

    Q_ASSERT(!icon.isNull());
    trayIcon.setIcon(icon);
#endif
}