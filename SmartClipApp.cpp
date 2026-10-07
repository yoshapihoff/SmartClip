#include "SmartClipApp.h"
#include "SettingsManager.h"
#include "SettingsDialog.h"
#include "HelpDialog.h"
#include "Version.h"
#include "HistoryManager.h"
#include "LaunchAgentManager.h"
#include "Crypto.h"
#include "MqttClient.h"
#include "SyncManager.h"
#include "TrayPopup.h"
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
#include <QGuiApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDateTime>
#include <QDialog>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>

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
    // Ключ шифрования нужен РАНЬШЕ загрузки настроек: им расшифровываются
    // пароли брокера и общий пароль синка в settings.yml.
    //
    // Шифрование истории — СТРОГОЕ условие: если крипто-бэкенд (OpenSSL)
    // или системное хранилище ключа недоступны, приложение не должно ни
    // писать, ни читать историю открытым текстом (fail-closed).
    const bool cryptoOk = Crypto::init();
    QByteArray encKey;
    if (cryptoOk)
        encKey = Crypto::loadOrCreateKey(QStringLiteral("SmartClip"),
                                         QStringLiteral("history-aes-key"));
    if (encKey.size() == 32) {
        settingsManager->setSecretKey(encKey);
    }
    settingsManager->loadSettings(settingsFilePath());

    // Apply launch at startup setting
    launchAgentManager->applyLaunchAtStartup(settingsManager->launchAtStartup());
    
    // ── Ключ шифрования истории ──────────────────────────────────────
    // Одна схема для всех платформ: AES-256-GCM, а ключ лежит в системном
    // хранилище (macOS Keychain / Linux Secret Service). Ключ создаётся
    // один раз и переиспользуется.
    if (encKey.size() == 32) {
        historyManager->setEncryptionKey(encKey);
        qInfo() << "SmartClip: шифрование истории ВКЛ ("
                << Crypto::keyringBackend() << ")";
    } else {
        // Fail-closed: без ключа история не пишется и не читается (см.
        // HistoryManager::save/loadHistory). Открытым текстом — никогда.
        qWarning() << "SmartClip: ключ шифрования недоступен — история буфера "
                      "НЕ сохраняется (шифрование строго обязательно).";
    }

    if (settingsManager->saveHistoryOnExit()) {
        historyManager->setMaxItems(settingsManager->maxItems());
        // Fail-closed: история есть на диске, но ключа нет — НЕ читаем и НЕ
        // перезаписываем (нельзя ни открыть, ни потерять). Громко сообщаем.
        if (encKey.size() != 32 && QFile::exists(historyFilePath())) {
            qWarning() << "SmartClip: история на диске есть, но ключ недоступен — "
                          "файл не читается и не перезаписывается.";
        }
        // loadHistory вернёт true, если файл был в старом ОТКРЫТОМ формате —
        // тогда перезапишем его шифрованным (разумая миграция).
        const bool migrated = historyManager->loadHistory(historyFilePath());
        // Восстанавливаем закреплённые цвета избранного из загруженной истории
        favoriteItemColors.clear();
        for (const auto &item : historyManager->history()) {
            if (item.favoriteColorIndex >= 0) {
                favoriteItemColors[item.text] = item.favoriteColorIndex;
            }
        }
        if (migrated) {
            qInfo() << "SmartClip: миграция истории → шифрованный формат";
            persistHistory();
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

    // ── Своё окно вместо меню десктопа (по умолчанию включено) ────────
    // Отключается SMARTCLIP_NO_TRAY_POPUP=1 — тогда работает нативное
    // меню десктопа (как было). На macOS — тот же путь (там тоже своё окно,
    // больше свободы в кастомизации).
    trayPopupEnabled = !qEnvironmentVariableIsSet("SMARTCLIP_NO_TRAY_POPUP");
    if (trayPopupEnabled) {
        trayPopup = new TrayPopup();
        trayPopup->setVersion(QStringLiteral(SMARTCLIP_VERSION_STRING));

        connect(trayPopup, &TrayPopup::clipChosen, this,
                [this](const QString &text) {
                    historyManager->incrementUsageCount(text);
                    if (QClipboard *clipboard = QApplication::clipboard()) {
                        ignoreNextClipboardChange = true;
                        clipboard->setText(text, QClipboard::Clipboard);
                    }
                    persistHistory();
                    notifySync();
                    rebuildMenu();
                });
        connect(trayPopup, &TrayPopup::favoriteToggled, this,
                &SmartClipApp::onToggleFavorite);
        connect(trayPopup, &TrayPopup::maskToggled, this,
                [this](const QString &text) { toggleItemMask(text); refreshTrayPopup(); });
        connect(trayPopup, &TrayPopup::commentRequested, this,
                [this](const QString &text) {
                    QString newComment;
                    if (promptComment(text, newComment)) {
                        historyManager->setComment(text, newComment);
                        persistHistory();
                        notifySync();
                    }
                    refreshTrayPopup();
                });
        connect(trayPopup, &TrayPopup::deleteRequested, this,
                [this](const QString &text) {
                    onDeleteItem(text);
                    refreshTrayPopup();
                });
        connect(trayPopup, &TrayPopup::clearRequested, this,
                &SmartClipApp::onClearHistory);
        connect(trayPopup, &TrayPopup::settingsRequested, this,
                &SmartClipApp::onSettings);
        connect(trayPopup, &TrayPopup::helpRequested, this,
                &SmartClipApp::onHelp);
        connect(trayPopup, &TrayPopup::quitRequested, this,
                &SmartClipApp::onQuit);
    }

    if (!trayPopupEnabled) {
        // Фолбэк: своё окно выключено → показываем нативное меню десктопа.
        trayIcon.setContextMenu(&trayMenu);
    }
    // ВАЖНО (macOS): если задать контекстное меню, клик по иконке открывает
    // ИМЕННО его, а сигнал activated НЕ приходит — своё окно тогда не показать.
    // Поэтому при включённом попапе нативное меню не назначаем: клик ловим
    // сами в activated и открываем своё окно (одинаково на macOS и Linux).
    trayIcon.setToolTip(QString("SmartClip %1").arg(SMARTCLIP_VERSION_STRING));
    
    // Обработчик кликов по иконке трея
    connect(&trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (trayPopupEnabled && trayPopup) {
            // Своё окно: открываем по любой активации (ЛКМ/ПКМ/средний),
            // встаём рядом с иконкой (или у курсора, если геометрия пуста —
            // на GNOME SNI trayIcon.geometry() может быть (0,0 0x0)).
            (void)reason;
            showTrayPopup();
            return;
        }
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

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    // macOS: polling нужен для надёжного отслеживания clipboard (Qt не всегда получает события)
    // Linux: polling страхует от проблем с QClipboard::dataChanged на Wayland
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

#if defined(Q_OS_LINUX)
    // На Linux/Wayland Qt НЕ получает colorScheme (Unknown), поэтому
    // colorSchemeChanged никогда не срабатывает — и иконка не меняется
    // при переключении темы «на лету». Периодически перечитываем тему
    // сами (gsettings), дёшево — раз в 5 с.
    iconThemeTimer = new QTimer(this);
    iconThemeTimer->setInterval(5000);
    connect(iconThemeTimer, &QTimer::timeout, this, &SmartClipApp::updateIcon);
    iconThemeTimer->start();
#endif

    // Авто-сохранение истории/избранного/масок: гарантия персистентности
    // независимо от способа завершения (SIGTERM/крэш/рестарт).
    historyAutosaveTimer = new QTimer(this);
    historyAutosaveTimer->setInterval(10000);
    connect(historyAutosaveTimer, &QTimer::timeout,
            this, &SmartClipApp::persistHistory);
    historyAutosaveTimer->start();

    // ── Сетевая синхронизация (MQTT, опционально) ──────────────────────
    // Без модуля Qt6::Mqtt MqttClient::available() == false, и синк — no-op.
    mqttClient = new MqttClient(this);
    syncManager = new SyncManager(mqttClient, historyManager, settingsManager, this);
    connect(syncManager, &SyncManager::stateApplied, this, [this]() {
        // Синхронизация могла ПЕРЕНАЗНАЧИТЬ цвета избранного (у ведомого
        // избранное получает следующие свободные цвета). Локальный кэш
        // favoriteItemColors иначе остался бы старым и перебивал бы
        // авторитетный favoriteColorIndex при отрисовке меню → маркеры
        // показывали бы прежние цвета. Поэтому пересобираем кэш из истории.
        favoriteItemColors.clear();
        for (const auto &item : historyManager->history()) {
            if (item.favoriteColorIndex >= 0)
                favoriteItemColors[item.text] = item.favoriteColorIndex;
        }
        // Пришло удалённое состояние → обновляем меню и сохраняем на диск.
        rebuildMenu();
        persistHistory();
    });
    syncManager->applySettings();
}



void SmartClipApp::show()
{
    trayIcon.show();
}

void SmartClipApp::notifySync()
{
    if (syncManager)
        syncManager->notifyLocalChange();
}

void SmartClipApp::handleClipboardChange()
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return;
    }

    const QString text = clipboard->text(QClipboard::Clipboard);
    // НЕ логируем содержимое буфера: туда попадают пароли, и они утекали бы
    // в journalctl/syslog открытым текстом. Логируем только факт и длину.
    qDebug() << "Clipboard changed, length:" << text.size();

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
    persistHistory();   // сохранить сразу (не ждать таймер)
    notifySync();       // поделиться новым клипом
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
    SettingsDialog dialog(settingsManager, syncManager);
    if (dialog.exec() == QDialog::Accepted) {
        // Settings are automatically saved by SettingsManager when changed
        // Apply launch at startup if changed
        launchAgentManager->applyLaunchAtStartup(settingsManager->launchAtStartup());
        
        // Trim history if max items changed
        historyManager->setMaxItems(settingsManager->maxItems());
        
        // Перезапустить сетевой синк под новые настройки (вкл/выкл/брокер).
        if (syncManager)
            syncManager->applySettings();
        
        // Rebuild menu to reflect any changes
        rebuildMenu();
    }
}

void SmartClipApp::persistHistory()
{
    // Избранное/маски/история должны переживать перезапуск НЕЗАВИСИМО от
    // того, как приложение завершилось. Раньше saveHistory звался только
    // в handleExitCleanup (чистый выход) — рестарт/kill/крэш терял изменения,
    // и избранное не сохранялось (симптом: «после перезапуска нет избранного»).
    if (!settingsManager->saveHistoryOnExit())
        return;
    if (!historyManager->isDirty())
        return;
    historyManager->saveHistory(historyFilePath());
    historyManager->clearDirty();
}

void SmartClipApp::toggleItemMask(const QString &text)
{
    historyManager->setMaskInMenu(text, !historyManager->maskInMenu(text));
    persistHistory();   // не терять зашифрованный режим при перезапуске
    notifySync();
}

bool SmartClipApp::promptComment(const QString &text, QString &out)
{
    // Маленькое модальное окно: показать запись, дать поле ввода комментария.
    QDialog dlg;
    dlg.setWindowTitle("Comment on clip");
    auto *lay = new QVBoxLayout(&dlg);
    auto *lbl = new QLabel("Clip:", &dlg);
    lay->addWidget(lbl);
    auto *shown = new QLabel(text.size() > 200 ? text.left(200) + "…" : text, &dlg);
    shown->setWordWrap(true);
    shown->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(shown);
    auto *clbl = new QLabel("Comment:", &dlg);
    lay->addWidget(clbl);
    auto *edit = new QPlainTextEdit(&dlg);
    edit->setPlainText(historyManager->comment(text));
    edit->setMinimumSize(360, 90);
    edit->selectAll();
    lay->addWidget(edit);
    auto *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    // Явно задаём английские подписи: иначе Qt подхватывает перевод
    // по локали (напр. ru_RU → «Окей/Отмена») и интерфейс становится
    // смешанным. Интерфейс приложения — единый английский.
    box->button(QDialogButtonBox::Ok)->setText("OK");
    box->button(QDialogButtonBox::Cancel)->setText("Cancel");
    QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.setWindowFlag(Qt::WindowStaysOnTopHint, true);

    // Одноразовый режим комментариев: гасим ТОЛЬКО его (clearModes() сбросил
    // бы и другие включённые режимы, напр. «вскрытие паролей»).
    QObject::connect(&dlg, &QDialog::finished, this, [this](int) {
        commentMode = false;
        if (commentModeAction) {
            QSignalBlocker b(commentModeAction);
            commentModeAction->setChecked(false);
        }
        rebuildMenu();
    });

    if (dlg.exec() == QDialog::Accepted) {
        out = edit->toPlainText();
        return true;
    }
    return false;
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
    persistHistory();
    handleExitCleanup();
    qApp->quit();
}

void SmartClipApp::onClearHistory()
{
    historyManager->clearHistory();
    if (historyManager->history().isEmpty()) {
        favoriteItemColors.clear();
    }
    persistHistory();
    notifySync();   // удаления (tombstone'ы) должны доехать до других устройств
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

    persistHistory();   // избранное должно пережить перезапуск
    notifySync();
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

void SmartClipApp::onDeleteItem(const QString &text)
{
    // Удаляем одну запись (режим «Удаление»). История хранит tombstone, поэтому
    // удаление синхронизируется на другие устройства БЕЗ приоритетов
    // master/slave — tombstone уезжает по MQTT как обычное состояние.
    historyManager->removeItem(text);
    releaseFavoriteColor(text);
    persistHistory();
    notifySync();
    // rebuildMenu() сделает вызвавший обработчик (после сброса режима).
}

void SmartClipApp::releaseFavoriteColor(const QString &text)
{
    favoriteItemColors.remove(text);
}

void SmartClipApp::clearModes()
{
    // Одноразовые режимы: после выполнения действия по клику режим сам гаснет.
    // Блокируем сигналы, чтобы тумблеры не вызывали лишний rebuildMenu().
    const bool needRebuild = favoriteMode || revealMode || commentMode || deleteMode;
    if (favoriteMode) {
        favoriteMode = false;
        if (favoriteModeAction) {
            QSignalBlocker b(favoriteModeAction);
            favoriteModeAction->setChecked(false);
        }
    }
    if (revealMode) {
        revealMode = false;
        if (revealModeAction) {
            QSignalBlocker b(revealModeAction);
            revealModeAction->setChecked(false);
        }
    }
    if (commentMode) {
        commentMode = false;
        if (commentModeAction) {
            QSignalBlocker b(commentModeAction);
            commentModeAction->setChecked(false);
        }
    }
    if (deleteMode) {
        deleteMode = false;
        if (deleteModeAction) {
            QSignalBlocker b(deleteModeAction);
            deleteModeAction->setChecked(false);
        }
    }
    Q_UNUSED(needRebuild);
}

void SmartClipApp::resetDeleteMode()
{
    // Гасим только режим удаления (клик по элементу в нём не должен
    // сбрасывать остальные включённые режимы).
    if (!deleteMode && (!deleteModeAction || !deleteModeAction->isChecked()))
        return;
    deleteMode = false;
    if (deleteModeAction) {
        QSignalBlocker b(deleteModeAction);
        deleteModeAction->setChecked(false);
    }
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
        // Метка следует своему флагу маскировки. В РЕЖИМЕ ВСКРЫТИЯ клик
        // переключает маску: показать пароль / снова скрыть (см. обработчик).
        const bool maskThis = history.at(i).maskInMenu;
        QString baseText = maskThis ? maskForMenuDisplay(text) : text;
        // Комментарий показываем после текста через тире (русское «тире»),
        // а не в скобках. Форматировать курсивом пункты меню нельзя ни на одной
        // из платформ (меню трея — нативные/DBus, только plain label).
        const QString cmt = history.at(i).comment;
        if (!cmt.isEmpty())
            baseText += QStringLiteral(" \u2014 ") + cmt;
        QAction *action = trayMenu.addAction(formatMenuLabel(baseText));

        // Показываем иконку избранного если элемент в избранном.
        // Цветной кружок — как в macOS-версии. ВАЖНО (02.10.2026): пункт
        // НЕ должен быть подменю — GNOME-расширение appindicator создаёт
        // `_icon` только для обычных пунктов, у PopupSubMenuMenuItem слота
        // иконки нет, и метка просто не рисовалась.
        if (history.at(i).favoriteColorIndex != -1) {
            action->setIcon(QIcon());
            action->setIconVisibleInMenu(true);
            QPixmap pixmap(12, 12);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);

            // Авторитетный источник цвета — favoriteColorIndex из HistoryItem
            // (его назначает SyncEngine::merge при синхронизации). Кэш
            // favoriteItemColors — ТОЛЬКО фолбэк, если поле невалидно:
            // раньше кэш перебивал его, и после синка у ведомого цвета
            // избранного оставались старыми.
            int colorIndex = history.at(i).favoriteColorIndex;
            if (colorIndex < 0 || colorIndex > 32) {
                colorIndex = favoriteItemColors.contains(text)
                                 ? favoriteItemColors[text]
                                 : 32; // fallback на белый
            }
            painter.setBrush(favoriteColors[colorIndex]);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(2, 2, 8, 8);
            painter.end();
            action->setIcon(QIcon(pixmap));
        }

        connect(action, &QAction::triggered, this, [this, text]() {
            // Ctrl/Shift+клик работает на macOS и X11. На Wayland (GNOME SNI)
            // модификаторы до нас НЕ доходят: расширение appindicator шлёт
            // Event('clicked', data=i 0), а Qt без фокуса окна отдаёт
            // NoModifier (QTBUG-105484). Поэтому есть явные режимы в меню:
            // «режим избранного» и «режим вскрытия паролей».
            const Qt::KeyboardModifiers mods =
                QGuiApplication::queryKeyboardModifiers();
            if (mods & Qt::ShiftModifier) {
                // Ctrl+Shift+Click — переключить зашифрованное отображение
                toggleItemMask(text);
            } else if ((mods & Qt::ControlModifier) || favoriteMode) {
                onToggleFavorite(text);
                clearModes();   // режим одноразовый — гасим после действия
            } else if (commentMode) {
                // РЕЖИМ КОММЕНТАРИЕВ: открыть модальное окно ввода.
                // «Окей» — сохранить (с шифрованием), «Отмена» — не трогать.
                // Режим гасится сам при закрытии окна (см. promptComment).
                QString newComment;
                if (promptComment(text, newComment)) {
                    historyManager->setComment(text, newComment);
                    persistHistory();
                    notifySync();
                }
            } else if (deleteMode) {
                // РЕЖИМ УДАЛЕНИЯ: клик удаляет элемент из списка, режим
                // гаснет. Удаление уезжает на другие устройства через
                // tombstone (без приоритетов master/slave).
                onDeleteItem(text);
                resetDeleteMode();
            } else if (revealMode) {
                // РЕЖИМ ВСКРЫТИЯ (пароли), как на macOS: клик переключает
                // маску элемента — скрытый пароль показывается, повторный
                // клик снова прячет. Этим же способом элемент ПОМЕЧАЕТСЯ как
                // пароль (на Wayland Shift+клик не доходит — см. выше).
                toggleItemMask(text);
                clearModes();   // режим одноразовый — гасим после действия
            } else {
                // Обычное копирование в буфер
                historyManager->incrementUsageCount(text);
                if (QClipboard *clipboard = QApplication::clipboard()) {
                    ignoreNextClipboardChange = true;
                    clipboard->setText(text, QClipboard::Clipboard);
                }
                persistHistory();
                notifySync();   // одиночный usesCount — не срочно, но пусть уходит
            }
            rebuildMenu();
        });
        action->setData(text); // Сохраняем текст для использования в контекстном меню
    }

    trayMenu.addSeparator();

    // «Режим избранного»: на GNOME Wayland Ctrl+клик не доходит до
    // приложения (см. обработчик пункта выше), поэтому даём явный тумблер.
    // Включён → клик по элементу добавляет/убирает его из избранного.
    if (!favoriteModeAction) {
        favoriteModeAction = new QAction(this);
        favoriteModeAction->setCheckable(true);
        connect(favoriteModeAction, &QAction::toggled, this, [this](bool on) {
            favoriteMode = on;
            rebuildMenu();
        });
    }
    favoriteModeAction->setChecked(favoriteMode);
    favoriteModeAction->setText(QStringLiteral("Favorite mode"));
    trayMenu.addAction(favoriteModeAction);

    // «Режим вскрытия паролей» (как на macOS): выключен — скрытый пункт
    // просто копируется в буфер; включён — клик по скрытому пункту показывает
    // пароль, повторный клик снова скрывает.
    if (!revealModeAction) {
        revealModeAction = new QAction(this);
        revealModeAction->setCheckable(true);
        connect(revealModeAction, &QAction::toggled, this, [this](bool on) {
            revealMode = on;
            rebuildMenu();
        });
    }
    revealModeAction->setChecked(revealMode);
    revealModeAction->setText(QStringLiteral("Reveal passwords"));
    trayMenu.addAction(revealModeAction);

    // «Режим комментариев»: клик по элементу открывает окно ввода комментария.
    if (!commentModeAction) {
        commentModeAction = new QAction(this);
        commentModeAction->setCheckable(true);
        connect(commentModeAction, &QAction::toggled, this, [this](bool on) {
            commentMode = on;
            rebuildMenu();
        });
    }
    commentModeAction->setChecked(commentMode);
    commentModeAction->setText(QStringLiteral("Comments"));
    trayMenu.addAction(commentModeAction);

    // «Режим удаления»: клик по элементу удаляет его из списка (одноразово).
    if (!deleteModeAction) {
        deleteModeAction = new QAction(this);
        deleteModeAction->setCheckable(true);
        connect(deleteModeAction, &QAction::toggled, this, [this](bool on) {
            deleteMode = on;
            rebuildMenu();
        });
    }
    deleteModeAction->setChecked(deleteMode);
    deleteModeAction->setText(QStringLiteral("Delete mode"));
    trayMenu.addAction(deleteModeAction);

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

    // Своё окно держим в актуальном состоянии вместе с меню.
    refreshTrayPopup();
}

void SmartClipApp::refreshTrayPopup()
{
    if (!trayPopup)
        return;

    // Цвет темы подхватываем системную (для ОКОН: 'default' = светлая).
    trayPopup->setDarkMode(windowPrefersDark());

    QVector<TrayPopup::RowData> rows;
    const auto &history = historyManager->history();
    rows.reserve(history.size());
    for (const auto &item : history) {
        TrayPopup::RowData r;
        r.text = item.text;
        r.display = item.maskInMenu ? maskForMenuDisplay(item.text) : item.text;
        r.display.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (r.display.size() > 90)
            r.display = r.display.left(87) + QStringLiteral("\u2026");
        if (!item.comment.isEmpty())
            r.display += QStringLiteral(" \u2014 ") + item.comment;
        r.masked = item.maskInMenu;
        if (item.favoriteColorIndex >= 0) {
            r.favorite = true;
            r.color = (item.favoriteColorIndex <= 32)
                          ? favoriteColors[item.favoriteColorIndex]
                          : favoriteColors[32];
        }
        rows.push_back(r);
    }
    trayPopup->setRows(rows);
}

void SmartClipApp::showTrayPopup()
{
    if (!trayPopup)
        return;
    refreshTrayPopup();

    // Точку клика берём от GNOME-расширения (Qt не отдаёт координаты из SNI):
    // оно пишет "x,y" в $XDG_RUNTIME_DIR/smartclip-tray-anchor. Файл свежий
    // (< 2 c) → встаём ровно под точкой клика; иначе — у курсора/иконки.
    QPoint anchor = QCursor::pos();
    bool haveAnchor = false;

    auto readAnchor = [](const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QPoint();
        const QByteArray data = f.readAll().trimmed();
        const QList<QByteArray> parts = data.split(',');
        bool ok1 = false, ok2 = false;
        const int x = parts.value(0).toInt(&ok1);
        const int y = parts.value(1).toInt(&ok2);
        return (ok1 && ok2) ? QPoint(x, y) : QPoint();
    };

    const QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!dir.isEmpty()) {
        const QString anchorFile = dir + "/smartclip-tray-anchor";
        QFileInfo fi(anchorFile);
        if (fi.exists()) {
            const QPoint p = readAnchor(anchorFile);
            // Свежесть: файл не старше 2 секунд (иначе это давний клик).
            if (!p.isNull()
                && fi.lastModified().secsTo(QDateTime::currentDateTime()) <= 2) {
                anchor = p;
                haveAnchor = true;
            }
        }
    }

    if (!haveAnchor) {
        // Фолбэк: у иконки трея, если geometry известна; иначе — у курсора.
        const QRect g = trayIcon.geometry();
        if (g.isValid() && !g.isEmpty())
            anchor = QPoint(g.center().x(), g.bottom());
    }
    trayPopup->showAt(anchor);
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

    const int maxLen = 90;   // больше места: комментарий идёт в скобках после текста
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

bool SmartClipApp::desktopPrefersDark()
{
    // 1) Qt 6.5+ знает тему сам — если ответ определённый, доверяем ему.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QStyleHints *h = qApp->styleHints()) {
        if (h->colorScheme() == Qt::ColorScheme::Dark)
            return true;
        if (h->colorScheme() == Qt::ColorScheme::Light)
            return false;
    }
#endif

    // 2) Фолбэк для Linux, где Qt НЕ отдаёт цветовую схему.
    //    Пример (наша система): GTK-тема светлая (adw-gtk3), а Qt
    //    colorScheme = Unknown, т.к. org.gnome.desktop.interface
    //    color-scheme = 'default'. При этом ВЕРХНЯЯ ПАНЕЛЬ GNOME тёмная,
    //    поэтому иконка трея обязана быть светлой. Смотрим в порядке
    //    приоритета: явная схема GNOME → тёмная GTK-тема → яркость
    //    палитры Qt.
#if defined(Q_OS_LINUX)
    auto readStr = [](const QString &prog, const QStringList &args) -> QString {
        QProcess p;
        p.start(prog, args);
        if (!p.waitForFinished(1500))
            return QString();
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    };

    // 2a) org.gnome.desktop.interface color-scheme = 'prefer-dark'
    const QString scheme = readStr("gsettings",
        {"get", "org.gnome.desktop.interface", "color-scheme"});
    if (scheme.contains("prefer-dark"))
        return true;
    if (scheme.contains("prefer-light"))
        return false;
    if (scheme.contains("default")) {
        // 'default' — тема не задаёт схему, НО у GNOME верхняя панель
        // (Quick Settings / обзор) по умолчанию ТЁМНАЯ. Считаем её тёмной,
        // иначе получаем чёрную иконку на тёмной панели — исходный баг.
        return true;
    }

    // 2b) тёмная GTK-тема по имени (…-dark) — если color-scheme не помог
    const QString gtkTheme = readStr("gsettings",
        {"get", "org.gnome.desktop.interface", "gtk-theme"});
    if (gtkTheme.toLower().contains("dark"))
        return true;
#endif

    // 3) Последний фолбэк — яркость палитры окна.
    return qApp->palette().color(QPalette::Window).lightness() < 128;
}

bool SmartClipApp::windowPrefersDark()
{
    // Для ОКОН (не иконки на панели). Qt 6.5+ знает схему точно — доверяем.
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QStyleHints *h = qApp->styleHints()) {
        if (h->colorScheme() == Qt::ColorScheme::Dark)
            return true;
        if (h->colorScheme() == Qt::ColorScheme::Light)
            return false;
    }
#endif

#if defined(Q_OS_LINUX)
    auto readStr = [](const QString &prog, const QStringList &args) -> QString {
        QProcess p;
        p.start(prog, args);
        if (!p.waitForFinished(1500))
            return QString();
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    };
    const QString scheme = readStr("gsettings",
        {"get", "org.gnome.desktop.interface", "color-scheme"});
    if (scheme.contains("prefer-dark"))
        return true;
    // ВАЖНО: 'default' для окон — это светлая тема (не тёмная, в отличие от
    // верхней панели GNOME). Ненавязчиво проверяем тёмную GTK-тему по имени.
    if (scheme.contains("prefer-light") || scheme.contains("default")) {
        const QString gtk = readStr("gsettings",
            {"get", "org.gnome.desktop.interface", "gtk-theme"});
        return gtk.toLower().contains("dark");
    }
    const QString gtk = readStr("gsettings",
        {"get", "org.gnome.desktop.interface", "gtk-theme"});
    if (gtk.toLower().contains("dark"))
        return true;
#endif

    return qApp->palette().color(QPalette::Window).lightness() < 128;
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
    const bool darkMode = desktopPrefersDark();

    // Кэш: не переставлять одну и ту же иконку на каждой проверке темы
    // (иначе трей перерисовывается зря — на некоторых DE мигает).
    if (iconDarkValid && iconDarkApplied == darkMode)
        return;
    iconDarkValid = true;
    iconDarkApplied = darkMode;

    QIcon icon = darkMode
        ? QIcon(":/icons/tray_white.svg")
        : QIcon(":/icons/tray_black.svg");

    Q_ASSERT(!icon.isNull());
    trayIcon.setIcon(icon);
#endif
}