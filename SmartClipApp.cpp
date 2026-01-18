#include "SmartClipApp.h"
#include "SettingsManager.h"
#include "SettingsDialog.h"
#include "HistoryManager.h"
#include "LaunchAgentManager.h"
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QIcon>
#include <QDebug>
#include <QMessageBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <QProcess>
#include <QPixmap>
#include <QPainter>

#include <algorithm>

#if defined(Q_OS_MAC)
 #include <unistd.h>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
 #include <QStyleHints>
#endif

// Инициализация статических полей
const QColor SmartClipApp::favoriteColors[8] = {
    QColor(255, 0, 0),   // красный
    QColor(255, 165, 0), // оранжевый
    QColor(255, 255, 0), // желтый
    QColor(0, 128, 0),   // зелёный
    QColor(0, 0, 255),   // голубой
    QColor(75, 0, 130),  // фиолетовый
    QColor(0, 191, 255), // синий
    QColor(255, 255, 255) // белый (для 8+ элементов)
};
int SmartClipApp::favoriteColorIndex = 0;

SmartClipApp::SmartClipApp(QObject *parent)
    : QObject(parent)
    , settingsManager(new SettingsManager(this))
    , historyManager(new HistoryManager(this))
    , launchAgentManager(new LaunchAgentManager(this))
{
    // Load settings
    settingsManager->loadSettings(settingsFilePath());
    
    // Apply launch at startup setting
    launchAgentManager->applyLaunchAtStartup(settingsManager->launchAtStartup());
    
    if (settingsManager->saveHistoryOnExit()) {
        loadHistory();
    } else {
        QFile::remove(historyFilePath());
    }
    updateIcon();

    titleAction = new QAction("Select the clip you want to add to your clipboard", this);
    titleAction->setEnabled(false);
    
    settingsAction = new QAction("Settings", this);
    connect(settingsAction, &QAction::triggered, this, &SmartClipApp::onSettings);

    clearHistoryAction = new QAction("Clear", this);
    connect(clearHistoryAction, &QAction::triggered, this, &SmartClipApp::onClearHistory);

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
        if (exitHandled) {
            return;
        }
        exitHandled = true;

        if (settingsManager->saveHistoryOnExit()) {
            if (historyManager->isDirty()) {
                saveHistory();
            }
        } else {
            QFile::remove(historyFilePath());
        }
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

void SmartClipApp::onClipboardChanged()
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
        return;
    }

    if (text == lastClipboardText) {
        return;
    }
    lastClipboardText = text;
    
    historyManager->addToHistory(text);
    rebuildMenu();
}

void SmartClipApp::pollClipboard()
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return;
    }

    const QString text = clipboard->text(QClipboard::Clipboard);
    
    if (ignoreNextClipboardChange) {
        ignoreNextClipboardChange = false;
        lastClipboardText = text;
        return;
    }

    if (text.trimmed().isEmpty()) {
        return;
    }

    if (text == lastClipboardText) {
        return;
    }
    lastClipboardText = text;
    
    historyManager->addToHistory(text);
    rebuildMenu();
}

void SmartClipApp::onSettings()
{
    SettingsDialog dialog(settingsManager);
    if (dialog.exec() == QDialog::Accepted) {
        // Settings were saved in the dialog
        // Apply launch at startup if changed
        launchAgentManager->applyLaunchAtStartup(settingsManager->launchAtStartup());
        
        // Trim history if max items changed
        historyManager->setMaxItems(settingsManager->maxItems());
        
        // Rebuild menu to reflect any changes
        rebuildMenu();
    }
}

void SmartClipApp::onQuit()
{
    if (!exitHandled) {
        exitHandled = true;
        if (settingsManager->saveHistoryOnExit()) {
            if (historyManager->isDirty()) {
                saveHistory();
            }
        } else {
            QFile::remove(historyFilePath());
        }
    }
    qApp->quit();
}

void SmartClipApp::onClearHistory()
{
    historyManager->clearHistory();
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
    } else {
        // Если элемент удаляется из избранного, освобождаем цвет
        releaseFavoriteColor(text);
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
        if (it.value() < 7) { // Игнорируем белый цвет
            usedColors.insert(it.value());
        }
    }
    
    // Находим первый свободный цвет
    for (int i = 0; i < 7; ++i) {
        if (!usedColors.contains(i)) {
            return i;
        }
    }
    
    // Если все цвета заняты, возвращаем белый (индекс 7)
    return 7;
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
        QAction *action = trayMenu.addAction(formatMenuLabel(text));

        // Показываем иконку избранного если элемент в избранном
        if (history.at(i).isFavorite) {
            action->setIcon(QIcon());
            action->setIconVisibleInMenu(true);
            QPixmap pixmap(12, 12);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            
            // Получаем закрепленный цвет за этим элементом
            int colorIndex = favoriteItemColors.value(text, 7); // По умолчанию белый
            painter.setBrush(favoriteColors[colorIndex]);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(2, 2, 8, 8);
            painter.end();
            action->setIcon(QIcon(pixmap));
        }
        
        connect(action, &QAction::triggered, this, [this, text]() {
            if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
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

void SmartClipApp::loadHistory()
{
    historyManager->loadHistory(historyFilePath());
}

void SmartClipApp::saveHistory() const
{
    historyManager->saveHistory(historyFilePath());
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