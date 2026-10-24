#include "SmartClipApp.h"
#include "SettingsManager.h"
#include "SettingsDialog.h"
#include "HelpDialog.h"
#include "AboutDialog.h"
#include "Version.h"
#include "HistoryManager.h"
#include "LaunchAgentManager.h"
#include "Crypto.h"
#include "MqttClient.h"
#include "SyncManager.h"
#include "TrayPopup.h"
#include "LayoutConverter.h"
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
#include <QWidget>
#include <QDialog>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>

#if defined(Q_OS_WIN)
// Тема системной панели (SystemUsesLightTheme) — для цвета иконки в трее.
// windows.h тянет за собой много макросов, поэтому подключаем его ПОСЛЕ
// Qt-заголовков и ограничиваем минимальный набор (WIN32_LEAN_AND_MEAN).
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <objbase.h>
#endif

#if defined(Q_OS_MAC)
// Вставка исправленного текста в прежнее окно: CGEvent (Ctrl/Cmd+V) и
// возврат фокуса приложению через NSRunningApplication.
#  include <ApplicationServices/ApplicationServices.h>
#  include <Carbon/Carbon.h>
#  if defined(__OBJC__)
#    import <AppKit/AppKit.h>
#  endif
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
 #include <QStyleHints>
#endif

#include <QAbstractNativeEventFilter>


#if defined(Q_OS_WIN)
// Фильтр системных сообщений Windows: WM_SETTINGCHANGE приходит, когда
// меняются параметры персонализации (в т.ч. цвет панели задач).
// Qt сигнал colorSchemeChanged в этом случае НЕ приходит, поэтому иконка
// в трее оставалась белой и после переключения панели на светлую.
class WindowsThemeEventFilter final : public QAbstractNativeEventFilter
{
public:
    explicit WindowsThemeEventFilter(SmartClipApp *app) : m_app(app) {}

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override
    {
        Q_UNUSED(result);
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")
            return false;

        MSG *msg = static_cast<MSG *>(message);
        if (!msg)
            return false;

        // WM_SETTINGCHANGE с «ImmersiveColorSet» — как раз смена оформления.
        if (msg->message == WM_SETTINGCHANGE) {
            if (m_app)
                QMetaObject::invokeMethod(m_app, "onThemeTick", Qt::QueuedConnection);
        }
        return false;  // не перехватываем — пусть обрабатывает Qt
    }

private:
    SmartClipApp *m_app = nullptr;
};
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
        qInfo() << "SmartClip: history encryption ON ("
                << Crypto::keyringBackend() << ")";
    } else {
        // Fail-closed: без ключа история не пишется и не читается (см.
        // HistoryManager::save/loadHistory). Открытым текстом — никогда.
        qWarning() << "SmartClip: encryption key unavailable — clipboard history "
                      "is NOT saved (encryption is strictly required).";
    }

    if (settingsManager->saveHistoryOnExit()) {
        historyManager->setMaxItems(settingsManager->maxItems());
        // Fail-closed: история есть на диске, но ключа нет — НЕ читаем и НЕ
        // перезаписываем (нельзя ни открыть, ни потерять). Громко сообщаем.
        if (encKey.size() != 32 && QFile::exists(historyFilePath())) {
            qWarning() << "SmartClip: history exists on disk but the key is "
                          "unavailable — the file is neither read nor rewritten.";
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
            qInfo() << "SmartClip: migrating history -> encrypted format";
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

    aboutAction = new QAction("About", this);
    connect(aboutAction, &QAction::triggered, this, &SmartClipApp::onAbout);

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
                [this](const QString &text) {
                    onToggleFavorite(text);
                    refreshTrayPopup();   // не прятать попап — обновить на месте
                });
        connect(trayPopup, &TrayPopup::maskToggled, this,
                [this](const QString &text) { toggleItemMask(text); refreshTrayPopup(); });
        connect(trayPopup, &TrayPopup::layoutConvertRequested, this,
                &SmartClipApp::onConvertLayout);
        // Клик по полосе «исправленный текст в буфере» — вставить в прежнее окно.
        connect(trayPopup, &TrayPopup::pasteIntoPreviousRequested, this,
                &SmartClipApp::onPasteConverted);
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
                [this]() {
                    onClearHistory();
                    refreshTrayPopup();   // осталось только избранное — обновить
                });
        connect(trayPopup, &TrayPopup::settingsRequested, this,
                &SmartClipApp::onSettings);
        connect(trayPopup, &TrayPopup::helpRequested, this,
                &SmartClipApp::onHelp);
        connect(trayPopup, &TrayPopup::aboutRequested, this,
                &SmartClipApp::onAbout);
        connect(trayPopup, &TrayPopup::quitRequested, this,
                &SmartClipApp::onQuit);

        // Закрытие попапа по клику МИМО окна: на Wayland XWayland-попап не
        // всегда ловит клик вне, поэтому GNOME-расширение пишет файл-сигнал.
        popupDismissTimer = new QTimer(this);
        popupDismissTimer->setInterval(400);
        connect(popupDismissTimer, &QTimer::timeout, this, [this]() {
            if (!trayPopup || !trayPopup->isVisible())
                return;
            const QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
            if (dir.isEmpty())
                return;
            QFileInfo fi(dir + "/smartclip-tray-dismiss");
            if (!fi.exists())
                return;
            // Сигнал свежее момента показа попапа → закрываем.
            if (fi.lastModified().toMSecsSinceEpoch() > popupShownAtMs)
                trayPopup->hide();
        });
        popupDismissTimer->start();
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
            // Своё окно: по активации иконки — ТОГГЛ. Если попап уже открыт,
            // повторный клик прячет его (как обычное меню), другой клик — снова
            // показывает рядом с иконкой (или у курсора, если геометрия пуста —
            // на GNOME SNI trayIcon.geometry() может быть (0,0 0x0)).
            (void)reason;
            if (trayPopup->isVisible())
                trayPopup->hide();
            else
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

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    // macOS: polling нужен для надёжного отслеживания clipboard (Qt не всегда получает события)
    // Linux: polling страхует от проблем с QClipboard::dataChanged на Wayland
    // Windows: polling — на случай, когда чужие приложения меняют буфер
    //          без уведомления Qt (наблюдается на практике)
    clipboardPollTimer = new QTimer(this);
    clipboardPollTimer->setInterval(500);
    connect(clipboardPollTimer, &QTimer::timeout, this, &SmartClipApp::pollClipboard);
    clipboardPollTimer->start();
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QStyleHints *hints = qApp->styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, &SmartClipApp::onThemeTick);
    }
#endif

#if defined(Q_OS_WIN)
    // Windows не всегда шлёт colorSchemeChanged при смене цвета панели задач
    // (это отдельная настройка SystemUsesLightTheme). Ловим системное
    // сообщение WM_SETTINGCHANGE — тогда иконка меняется мгновенно,
    // а не только по таймеру.
    nativeEventFilter = new WindowsThemeEventFilter(this);
    qApp->installNativeEventFilter(nativeEventFilter);
#endif

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
    // На Linux/Wayland Qt НЕ получает colorScheme (Unknown), поэтому
    // colorSchemeChanged никогда не срабатывает — и иконка не меняется
    // при переключении темы «на лету».
    //
    // На Windows та же беда, но по другой причине: цвет ПАНЕЛИ ЗАДАЧ
    // (SystemUsesLightTheme) можно поменять в параметрах, а Qt при этом
    // не кидает colorSchemeChanged — иконка оставалась прежнего цвета.
    // Поэтому периодически перечитываем тему сами, дёшево — раз в 5 с.
    iconThemeTimer = new QTimer(this);
    iconThemeTimer->setInterval(5000);
    connect(iconThemeTimer, &QTimer::timeout, this, &SmartClipApp::onThemeTick);
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
        // Если попап открыт — пересобираем его тоже, иначе он показывает
        // состояние ДО синка и выглядит так, будто элементы «исчезли» или
        // «не приехали» (жалоба 12.10.2026).
        if (trayPopup && trayPopup->isVisible())
            refreshTrayPopup();
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
    // Закрываем попап перед модальным диалогом (запрос Лёши: прятать окно
    // только при настройках/справке/копировании).
    if (trayPopup)
        trayPopup->hide();
    HelpDialog dialog;
    dialog.exec();
}

void SmartClipApp::onAbout()
{
    // Как справка/настройки: модальный диалог — попап прячем.
    if (trayPopup)
        trayPopup->hide();
    AboutDialog dialog;
    dialog.exec();
}

void SmartClipApp::onSettings()
{
    // Аналогично справке: модальный диалог → попап прячем.
    if (trayPopup)
        trayPopup->hide();
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
    layoutOfItem.clear();
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

void SmartClipApp::onConvertLayout(const QString &text)
{
    const QStringList layouts = LayoutConverter::availableLayouts();
    if (layouts.size() < 2) {
        refreshTrayPopup();
        return;
    }

    // Исходник текущего исправления: ещё раз жать не надо — новый текст уже
    // в буфере, а «переконвертировать» исходник можно только после того, как
    // исправление применено или отменено (см. onPasteConverted/отмену).
    if (converted.active && text == converted.sourceText) {
        refreshTrayPopup();
        return;
    }

    // Текущая раскладка именно этого элемента. Если мы её ещё не помним
    // (запись только что появилась/пришла по синку) — определяем по самому
    // тексту, чтобы работали оба направления (us→ru и ru→us).
    QString current = layoutOfItem.value(text);
    if (!layouts.contains(current))
        current = LayoutConverter::guessLayout(text);
    if (current.isEmpty())
        current = layouts.first();
    const QString next = LayoutConverter::nextLayout(current);
    const QString converted = LayoutConverter::convert(text, current, next);
    if (converted.isEmpty()) {
        // Ни один символ не удалось перекодировать (например, в тексте нет
        // букв, которые есть на клавишах исходной раскладки) — буфер не трогаем.
        refreshTrayPopup();
        return;
    }

    // Заменяем текст записи, перенося избранное/маску/комментарий/счётчик.
    const bool removedPrefix = historyManager->renameItem(text, converted);
    if (removedPrefix)
        releaseFavoriteColor(text);
    layoutOfItem.remove(text);
    layoutOfItem[converted] = next;

    // Исправленный текст — в буфер обмена (решение Лёши 24.10.2026): так его
    // можно вставить в ЛЮБОЕ поле — из меню трея, из другого приложения,
    // горячей клавишей. Порядок важен: сначала не даём SmartClip
    // зарегистрировать собственную запись как «новую копию», потом пишем.
    //
    // Отдельно запоминаем, что лежало в буфере ДО и какое окно было активно:
    // если пользователь сразу откроет попап, полоса сверху предложит вставить
    // исправленное в прежнее окно, а отмена вернёт прежний буфер.
    QClipboard *clipboard = QApplication::clipboard();
    QString prevText;
    if (clipboard) {
        prevText = clipboard->text(QClipboard::Clipboard);
        ignoreNextClipboardChange = true;
        clipboard->setText(converted, QClipboard::Clipboard);
        lastClipboardText = converted;   // polling не должен принять это за копию
    }
    // Помним исправление: полоса попапа предложит вставить его в прежнее
    // окно, а отмена (revertConvertedClipboard) вернёт прежний буфер.
    this->converted.active = true;
    this->converted.text = converted;
    this->converted.original = prevText;
    this->converted.sourceText = text;
    this->converted.layout = next;

    persistHistory();
    notifySync();
    refreshTrayPopup();   // попап пересобирается: полоса + пометка строки-исходника
}

// ── Смена раскладки + буфер: возврат, вставка в прежнее окно ──────────────
//
// Зачем так: текст, сменивший раскладку, кладём в буфер СРАЗУ (его можно
// вставить хоть горячей клавишей, хоть из меню), но «применить» его в чужое
// окно — только по явному действию пользователя. Иначе новый текст затирает
// то, что человек как раз собирался вставить (кнопка «вставить» в КУДА надо
// была нажата до того, как SmartClip перезаписал буфер).

void SmartClipApp::revertConvertedClipboard()
{
    if (!converted.active)
        return;
    if (QClipboard *clipboard = QApplication::clipboard()) {
        ignoreNextClipboardChange = true;
        clipboard->setText(converted.original, QClipboard::Clipboard);
        lastClipboardText = converted.original;
    }
    converted = ConvertedClip();
    refreshTrayPopup();
}

bool SmartClipApp::syntheticPasteAvailable()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    // Linux/Wayland: универсального синтетического ввода без спецразрешений
    // нет (ydotool/wtype — отдельные демоны с правами root), поэтому там
    // кнопка «Paste» не показывается: текст просто лежит в буфере.
    return true;
#else
    return false;
#endif
}

void SmartClipApp::rememberPreviousTarget()
{
#if defined(Q_OS_WIN)
    // Активное окно ДО открытия попапа — туда вернём фокус перед Ctrl+V.
    converted.prevWnd = reinterpret_cast<quintptr>(::GetForegroundWindow());
#elif defined(Q_OS_MAC)
    // macOS: попап показан через no-activate путь (LSUIElement), поэтому
    // предыдущее приложение обычно всё ещё активно — запоминаем его.
    if (!converted.prevApp) {
        ProcessSerialNumber psn = {0, kNoProcess};
        ProcessSerialNumber self = {0, kNoProcess};
        if (::GetCurrentProcess(&self) == noErr
            && ::GetFrontProcess(&psn) == noErr
            && !(psn.lowLongOfPSN == self.lowLongOfPSN
                 && psn.highLongOfPSN == self.highLongOfPSN)) {
            converted.prevApp = psn.lowLongOfPSN
                    ? reinterpret_cast<void *>(static_cast<quintptr>(psn.highLongOfPSN) << 32
                                               | static_cast<quintptr>(psn.lowLongOfPSN))
                    : nullptr;
        }
    }
#endif
}

void SmartClipApp::onPasteConverted()
{
    if (!converted.active || converted.text.isEmpty())
        return;
    const QString text = converted.text;
    if (!pasteConvertedIntoPrevious()) {
        // Не смогли нажать Ctrl/Cmd+V (нет разрешения или платформа не умеет):
        // текст уже в буфере — просто просим вставить вручную.
        if (trayPopup)
            trayPopup->hide();
        if (QClipboard *clipboard = QApplication::clipboard()) {
            ignoreNextClipboardChange = true;
            clipboard->setText(text, QClipboard::Clipboard);
            lastClipboardText = text;
        }
        qInfo() << "SmartClip: автоматическая вставка недоступна — текст оставлен в буфере";
        return;
    }
    // Вставка ушла в прежнее окно — «исправление» применено, состояние снимаем.
    converted = ConvertedClip();
    refreshTrayPopup();
}

bool SmartClipApp::pasteConvertedIntoPrevious()
{
#if defined(Q_OS_WIN)
    const HWND hwnd = reinterpret_cast<HWND>(converted.prevWnd);
    if (!hwnd || !::IsWindow(hwnd))
        return false;
    // Фокус чужому окну: системный Alt-приём (обход запрета SetForegroundWindow
    // из неактивного процесса) + само возвращение фокуса.
    if (::GetForegroundWindow() != hwnd) {
        const DWORD fgThread = ::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr);
        const DWORD thisThread = ::GetCurrentThreadId();
        const bool attached = fgThread && fgThread != thisThread
                && ::AttachThreadInput(thisThread, fgThread, TRUE);
        ::SetForegroundWindow(hwnd);
        ::BringWindowToTop(hwnd);
        if (attached)
            ::AttachThreadInput(thisThread, fgThread, FALSE);
    }
    // Дадим чужому окну обработать активацию, иначе Ctrl+V уйдёт «в пустоту».
    ::Sleep(60);

    INPUT in[4] = {};
    const auto fill = [&in](int i, WORD vk, bool up) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = vk;
        in[i].ki.wScan = static_cast<WORD>(::MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        in[i].ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    };
    fill(0, VK_CONTROL, false);
    fill(1, 'V', false);
    fill(2, 'V', true);
    fill(3, VK_CONTROL, true);
    const UINT sent = ::SendInput(4, in, sizeof(INPUT));
    return sent == 4;
#elif defined(Q_OS_MAC)
    // macOS: синтетический Cmd+V через CGEvent. Нужно разрешение
    // «Универсальный доступ» (Accessibility) — без него события режет TCC.
    if (!converted.prevApp)
        return false;
    const quintptr packed = reinterpret_cast<quintptr>(converted.prevApp);
    ProcessSerialNumber psn;
    psn.highLongOfPSN = static_cast<UInt32>(packed >> 32);
    psn.lowLongOfPSN = static_cast<UInt32>(packed & 0xffffffffu);
    ::SetFrontProcess(&psn);
    ::CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.12, false);

    CGEventSourceRef src = ::CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!src)
        return false;
    CGEventRef down = ::CGEventCreateKeyboardEvent(src, (CGKeyCode)kVK_ANSI_V, true);
    CGEventRef up = ::CGEventCreateKeyboardEvent(src, (CGKeyCode)kVK_ANSI_V, false);
    if (!down || !up) {
        if (down) ::CFRelease(down);
        if (up) ::CFRelease(up);
        ::CFRelease(src);
        return false;
    }
    ::CGEventSetFlags(down, kCGEventFlagMaskCommand);
    ::CGEventSetFlags(up, kCGEventFlagMaskCommand);
    ::CGEventPost(kCGHIDEventTap, down);
    ::CGEventPost(kCGHIDEventTap, up);
    ::CFRelease(down);
    ::CFRelease(up);
    ::CFRelease(src);
    return true;
#else
    // Linux/Wayland: синтетического ввода без спецдемонов нет.
    return false;
#endif
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
    layoutOfItem.remove(text);
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

    if (aboutAction) {
        trayMenu.addAction(aboutAction);
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

    // Сам текст элемента в списке — не длиннее 32 символов (эллипсис при
    // превышении). Полный текст остаётся в r.text и копируется в буфер как есть.
    constexpr int kMaxItemChars = 32;

    // Цвет темы подхватываем системную (для ОКОН: 'default' = светлая).
    updatePopupTheme();

    QVector<TrayPopup::RowData> rows;
    const auto &history = historyManager->history();
    rows.reserve(history.size());
    // Кнопка смены раскладки появляется, только если в системе ≥ 2 раскладки.
    const bool layoutOk = LayoutConverter::available();
    for (const auto &item : history) {
        TrayPopup::RowData r;
        r.text = item.text;
        r.convertedSource = converted.active && !converted.sourceText.isEmpty()
                && item.text == converted.sourceText;
        r.display = item.maskInMenu ? maskForMenuDisplay(item.text) : item.text;
        r.display.replace(QLatin1Char('\n'), QLatin1Char(' '));
        if (r.display.size() > kMaxItemChars)
            r.display = r.display.left(kMaxItemChars - 1) + QStringLiteral("\u2026");
        // Комментарий НЕ режем — показывается целиком отдельным (нежирным)
        // элементом после текста (см. TrayPopup::setRows). Ширина попапа
        // подстраивается под содержимое (updateContentWidth).
        r.comment = item.comment;
        r.masked = item.maskInMenu;
        r.layoutConvertible = layoutOk;
        if (item.favoriteColorIndex >= 0) {
            r.favorite = true;
            r.color = (item.favoriteColorIndex <= 32)
                          ? favoriteColors[item.favoriteColorIndex]
                          : favoriteColors[32];
        }
        rows.push_back(r);
    }
    // Чистим память раскладок для записей, которых больше нет в истории.
    if (!layoutOfItem.isEmpty()) {
        QSet<QString> live;
        live.reserve(history.size());
        for (const auto &item : history)
            live.insert(item.text);
        for (auto it = layoutOfItem.begin(); it != layoutOfItem.end();) {
            if (!live.contains(it.key()))
                it = layoutOfItem.erase(it);
            else
                ++it;
        }
    }
    trayPopup->setRows(rows);
    // Полоса «исправленный текст в буфере»: видна, пока помним последнюю
    // смену раскладки и её результат ещё лежит в буфере (или сразу после).
    if (converted.active) {
        if (QClipboard *clipboard = QApplication::clipboard()) {
            if (!converted.text.isEmpty()
                && clipboard->text(QClipboard::Clipboard) == converted.text) {
                trayPopup->setConvertedText(converted.text);
            } else {
                // Пользователь уже скопировал что-то другое — полоса не нужна.
                trayPopup->setConvertedText(QString());
            }
        }
    } else {
        trayPopup->setConvertedText(QString());
    }
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
    popupShownAtMs = QDateTime::currentMSecsSinceEpoch();
    // Запоминаем, чьё окно было активно ДО открытия попапа: туда вернём фокус,
    // когда пользователь попросит вставить исправленный текст.
    rememberPreviousTarget();
#if defined(Q_OS_MAC)
    macBringAppForward();
#endif
    trayPopup->showAt(anchor);
}

#if defined(Q_OS_MAC)
void SmartClipApp::macBringAppForward()
{
    // macOS: menu-bar-приложение (LSUIElement) не является активным. Из-за
    // этого Qt::Popup после show() сразу закрывается системой → попап
    // «моргает» (виден долю секунды и гаснет), особенно при повторном клике
    // по иконке трея. Обходной путь (forum.qt.io/topic/164883): перед показом
    // попапа открыть и активировать невидимое окно-хелпер, чтобы приложение
    // было выведено вперёд и попап получил фокус. Окно не закрываем — это
    // безопаснее (закрытие в hideEvent давало краш).
    if (!macFocusHelper) {
        macFocusHelper = new QWidget();
        macFocusHelper->setAttribute(Qt::WA_TranslucentBackground);
        macFocusHelper->setWindowFlags(Qt::FramelessWindowHint);
        macFocusHelper->setFixedSize(2, 2);
    }
    macFocusHelper->show();
    macFocusHelper->raise();
    macFocusHelper->activateWindow();
}
#endif

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
#if defined(Q_OS_WIN)
    // Windows: тему ПАНЕЛИ ЗАДАЧ нельзя угадывать по colorScheme() — Qt
    // отдаёт схему приложений, а панель живёт своей жизнью (тёмная панель
    // при светлой теме — обычное дело). Спрашиваем сам Windows:
    // SystemUsesLightTheme (HKCU\...\Themes\Personalize) = 1 → СВЕТЛАЯ панель.
    // Реестр может отсутствовать (старые сборки) — тогда фолбэк ниже.
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                0, KEY_READ, &key) == ERROR_SUCCESS) {
            DWORD value = 0;
            DWORD size = sizeof(value);
            DWORD type = 0;
            const LONG rc = RegQueryValueExW(key, L"SystemUsesLightTheme", nullptr,
                                             &type, reinterpret_cast<LPBYTE>(&value), &size);
            RegCloseKey(key);
            if (rc == ERROR_SUCCESS && type == REG_DWORD)
                return value == 0;  // 0 → тёмная панель → нужна белая иконка
        }
    }
    // Фолбэк: системная тема через uxtheme (1 = тёмное приложение).
    // На панели задач это работает хуже, но лучше, чем ничего.
#endif

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

void SmartClipApp::updatePopupTheme()
{
    if (trayPopup)
        trayPopup->setDarkMode(windowPrefersDark());
}

void SmartClipApp::onThemeTick()
{
    // Один общий тик на Linux/Wayland (Qt не шлёт colorSchemeChanged):
    // обновляем и иконку трея, и тему уже открытого попапа. Иначе попап,
    // открытый во время смены темы, оставался в старой схеме до следующего
    // открытия (жалоба: «на тёмном и на светлом попап белый»).
    updateIcon();
    updatePopupTheme();
}


void SmartClipApp::updateIcon()
{
#if defined(Q_OS_MAC)
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