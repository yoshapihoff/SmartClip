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
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <QKeyEvent>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCryptographicHash>
#include <algorithm>
#include <QStyleHints>

#if defined(Q_OS_MAC)
 #include <unistd.h>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
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
        
        // Определяем нужно ли маскировать текст
        QString displayText = maskedItems.contains(text) ? maskText(text) : text;
        QAction *action = trayMenu.addAction(formatMenuLabel(displayText));

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
            Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
            
            if (modifiers & Qt::ControlModifier && modifiers & Qt::ShiftModifier) {
                // Shift+Ctrl+клик - переключаем маскирование
                toggleMaskItem(text);
            } else if (modifiers & Qt::ControlModifier) {
                onToggleFavorite(text);
            } else {
                // Обычное копирование в буфер - всегда копируем полный текст!
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

void SmartClipApp::toggleMaskItem(const QString &text)
{
    if (maskedItems.contains(text)) {
        maskedItems.remove(text);
    } else {
        maskedItems[text] = true;
    }
    rebuildMenu();
}

QString SmartClipApp::maskText(const QString &text) const
{
    if (text.length() <= 6) {
        return QString(text.length(), '*');
    }
    
    // Показываем первые 3 и последние 3 символа, остальное звездочками
    QString masked = text.left(3) + QString(text.length() - 6, '*') + text.right(3);
    return masked;
}

QByteArray SmartClipApp::getEncryptionKey() const
{
    QString keyPath = QDir::homePath() + QLatin1String("/.smartclip/.key");
    QFile keyFile(keyPath);
    
    // Если ключ существует, читаем его
    if (keyFile.open(QIODevice::ReadOnly)) {
        QByteArray storedKey = keyFile.readAll();
        if (storedKey.size() == 32) { // SHA-256 = 32 байта
            return QByteArray::fromBase64(storedKey);
        }
    }
    
    // Генерируем новый ключ
    QByteArray newKey = QCryptographicHash::hash(
        QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8() + 
        QString::number(QCoreApplication::applicationPid()).toUtf8(),
        QCryptographicHash::Sha256
    );
    
    // Сохраняем ключ (только для чтения/записи владельцем)
    if (keyFile.open(QIODevice::WriteOnly)) {
        keyFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        keyFile.write(newKey.toBase64());
    }
    
    return newKey;
}

QByteArray SmartClipApp::encryptData(const QByteArray &data) const
{
    QByteArray key = getEncryptionKey();
    
    // Простой но надежный метод: XOR с хэшем ключа
    QByteArray encrypted = data;
    for (int i = 0; i < encrypted.size(); ++i) {
        encrypted[i] = encrypted[i] ^ key[i % key.size()];
    }
    
    // Добавляем проверочную сумму для целостности
    QByteArray hash = QCryptographicHash::hash(encrypted, QCryptographicHash::Sha256);
    encrypted.append(hash.left(8)); // 8 байт контрольной суммы
    
    return encrypted;
}

QByteArray SmartClipApp::decryptData(const QByteArray &data) const
{
    if (data.size() < 8) {
        return QByteArray(); // Слишком короткие данные
    }
    
    QByteArray key = getEncryptionKey();
    
    // Отделяем данные от контрольной суммы
    QByteArray encrypted = data.left(data.size() - 8);
    QByteArray storedHash = data.right(8);
    
    // Расшифровываем
    QByteArray decrypted = encrypted;
    for (int i = 0; i < decrypted.size(); ++i) {
        decrypted[i] = decrypted[i] ^ key[i % key.size()];
    }
    
    // Проверяем целостность
    QByteArray calculatedHash = QCryptographicHash::hash(encrypted, QCryptographicHash::Sha256).left(8);
    if (storedHash != calculatedHash) {
        qDebug() << "Data integrity check failed!";
        return QByteArray(); // Данные повреждены
    }
    
    return decrypted;
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
    // Загружаем зашифрованный файл истории
    QFile file(historyFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray encryptedData = file.readAll();
        QByteArray decryptedData = decryptData(encryptedData);
        
        // Парсим расшифрованные данные
        QString content = QString::fromUtf8(decryptedData);
        QStringList lines = content.split('\n');
        
        HistoryManager::HistoryItem currentItem;
        bool hasItem = false;
        
        for (const QString &line : lines) {
            if (line.startsWith("text:\"")) {
                if (hasItem) {
                    // Сохраняем предыдущий элемент
                    historyManager->addToHistory(currentItem.text);
                    // Устанавливаем остальные параметры
                    const auto &history = historyManager->history();
                    if (!history.isEmpty()) {
                        auto &lastItem = const_cast<HistoryManager::HistoryItem&>(history.last());
                        lastItem.isFavorite = currentItem.isFavorite;
                        lastItem.usageCount = currentItem.usageCount;
                        lastItem.addedAtMs = currentItem.addedAtMs;
                    }
                }
                
                currentItem.text = line.mid(6, line.length() - 7); // Убираем text:" и "
                currentItem.isFavorite = false;
                currentItem.usageCount = 0;
                currentItem.addedAtMs = QDateTime::currentMSecsSinceEpoch();
                hasItem = true;
            } else if (line.startsWith("favorite:")) {
                currentItem.isFavorite = (line.mid(9) == "true");
            } else if (line.startsWith("count:")) {
                currentItem.usageCount = line.mid(6).toInt();
            } else if (line.startsWith("time:")) {
                currentItem.addedAtMs = line.mid(5).toLongLong();
            } else if (line == "---") {
                if (hasItem) {
                    historyManager->addToHistory(currentItem.text);
                    // Устанавливаем остальные параметры
                    const auto &history = historyManager->history();
                    if (!history.isEmpty()) {
                        auto &lastItem = const_cast<HistoryManager::HistoryItem&>(history.last());
                        lastItem.isFavorite = currentItem.isFavorite;
                        lastItem.usageCount = currentItem.usageCount;
                        lastItem.addedAtMs = currentItem.addedAtMs;
                    }
                    hasItem = false;
                }
            }
        }
        
        // Сохраняем последний элемент
        if (hasItem) {
            historyManager->addToHistory(currentItem.text);
            // Устанавливаем остальные параметры
            const auto &history = historyManager->history();
            if (!history.isEmpty()) {
                auto &lastItem = const_cast<HistoryManager::HistoryItem&>(history.last());
                lastItem.isFavorite = currentItem.isFavorite;
                lastItem.usageCount = currentItem.usageCount;
                lastItem.addedAtMs = currentItem.addedAtMs;
            }
        }
    }
    
    // Загружаем метаданные (расшифровываем весь файл)
    QFile metaFile(QDir::homePath() + QLatin1String("/.smartclip/metadata.yml"));
    if (metaFile.open(QIODevice::ReadOnly)) {
        QByteArray encryptedData = metaFile.readAll();
        QByteArray decryptedData = decryptData(encryptedData);
        
        // Парсим расшифрованные метаданные
        QString content = QString::fromUtf8(decryptedData);
        QStringList lines = content.split('\n');
        
        for (const QString &line : lines) {
            if (line.startsWith("masked:\"")) {
                QString text = line.mid(8, line.length() - 9); // Убираем masked:" и "
                maskedItems[text] = true;
            } else if (line.startsWith("color:\"")) {
                int colonPos = line.indexOf("\":");
                if (colonPos > 7) {
                    QString text = line.mid(7, colonPos - 7); // Убираем color:" и "
                    int colorIndex = line.mid(colonPos + 2).toInt();
                    favoriteItemColors[text] = colorIndex;
                }
            }
        }
    }
}

void SmartClipApp::saveHistory() const
{
    // Сохраняем историю в обычном формате
    QString tempPath = historyFilePath() + ".tmp";
    QFile tempFile(tempPath);
    if (tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&tempFile);
        
        const auto &history = historyManager->history();
        for (const auto &item : history) {
            out << "text:\"" << item.text << "\"\n";
            out << "favorite:" << (item.isFavorite ? "true" : "false") << "\n";
            out << "count:" << item.usageCount << "\n";
            out << "time:" << item.addedAtMs << "\n";
            out << "---\n";
        }
    }
    
    // Шифруем весь файл и перезаписываем оригинал
    QFile tempRead(tempPath);
    if (tempRead.open(QIODevice::ReadOnly)) {
        QByteArray plainData = tempRead.readAll();
        QByteArray encryptedData = encryptData(plainData);
        
        QFile encryptedFile(historyFilePath());
        if (encryptedFile.open(QIODevice::WriteOnly)) {
            encryptedFile.write(encryptedData);
        }
    }
    
    // Удаляем временный файл
    tempFile.remove();
    
    // Сохраняем метаданные (шифруем весь файл)
    QString metaPath = QDir::homePath() + QLatin1String("/.smartclip/metadata.yml");
    QString metaTempPath = metaPath + ".tmp";
    QFile metaTempFile(metaTempPath);
    if (metaTempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&metaTempFile);
        out << "# SmartClip metadata\n";
        out << "# Masked items\n";
        for (auto it = maskedItems.begin(); it != maskedItems.end(); ++it) {
            out << "masked:\"" << it.key() << "\"\n";
        }
        out << "# Favorite colors\n";
        for (auto it = favoriteItemColors.begin(); it != favoriteItemColors.end(); ++it) {
            out << "color:\"" << it.key() << "\":" << it.value() << "\n";
        }
    }
    
    // Шифруем весь файл метаданных
    QFile metaTempRead(metaTempPath);
    if (metaTempRead.open(QIODevice::ReadOnly)) {
        QByteArray plainData = metaTempRead.readAll();
        QByteArray encryptedData = encryptData(plainData);
        
        QFile metaEncryptedFile(metaPath);
        if (metaEncryptedFile.open(QIODevice::WriteOnly)) {
            metaEncryptedFile.write(encryptedData);
        }
    }
    
    // Удаляем временный файл метаданных
    metaTempFile.remove();
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