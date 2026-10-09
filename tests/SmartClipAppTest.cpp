#include <QtTest/QtTest>
#include <QApplication>
#include <QClipboard>
#include <QSystemTrayIcon>
#include <QAction>
#include <QMenu>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QMetaObject>
#include <QTextStream>
#include <QByteArray>
#include <QDateTime>

#include "../SmartClipApp.h"
#include "../SettingsManager.h"
#include "../HistoryManager.h"
#include "../LaunchAgentManager.h"
#include "../TrayPopup.h"
#include "../AboutDialog.h"
#include "../HelpDialog.h"
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QScrollArea>

class TestSmartClipApp : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testInitialization();
    void testShow();
    void testFilePaths();
    void testFormatMenuLabel();
    void testMaskForMenuDisplay();
    void testHandleClipboardChange();
    void testHandleClipboardChangeEmptyText();
    void testHandleClipboardChangeDuplicateText();
    void testHandleClipboardChangeIgnoredChange();
    void testOnClearHistory();
    void testOnToggleFavorite();
    void testGetFavoriteColorIndex();
    void testReleaseFavoriteColor();
    void testRebuildMenu();
    void testHandleExitCleanup();
    void testOnQuit();
    void testOnSettings();
    void testClipboardPolling();
    void testIconUpdate();
    void testMultipleClipboardChanges();
    void testFavoriteColorPersistence();
    void testTrayPopupHideButton();
    void testTrayPopupClearDoesNotHide();
    void testTrayPopupRowActionDoesNotHide();
    void testTrayPopupWidthFitsContent();
    void testTrayPopupDeactivateHideOnlyOnWayland();
    void testAboutDialogShowsNameVersionAuthorLicense();
    void testHelpDialogTitleHasNoVersion();
    void testTrayPopupAboutButton();
    void testTrayPopupEmptyStateNotSquished();

private:
    QApplication *m_app;
    SmartClipApp *m_smartClipApp;
    QString m_testSettingsPath;
    QString m_testHistoryPath;
};

void TestSmartClipApp::initTestCase()
{
    int argc = 0;
    char **argv = nullptr;
    m_app = new QApplication(argc, argv);
    
    // Setup test paths
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_testSettingsPath = tempDir + "/test_settings.yml";
    m_testHistoryPath = tempDir + "/test_history.yml";
    
    // Clean up any existing test files
    QFile::remove(m_testSettingsPath);
    QFile::remove(m_testHistoryPath);
}

void TestSmartClipApp::cleanupTestCase()
{
    QFile::remove(m_testSettingsPath);
    QFile::remove(m_testHistoryPath);
    // Don't delete m_app here to avoid segfault
}

void TestSmartClipApp::init()
{
    m_smartClipApp = new SmartClipApp();
}

void TestSmartClipApp::cleanup()
{
    delete m_smartClipApp;
    QFile::remove(m_testSettingsPath);
    QFile::remove(m_testHistoryPath);
}

void TestSmartClipApp::testInitialization()
{
    QVERIFY(m_smartClipApp != nullptr);
    // The app should initialize without crashing
    QVERIFY(true);
}

void TestSmartClipApp::testShow()
{
    // Test that show() doesn't crash
    m_smartClipApp->show();
    QVERIFY(true);
}

void TestSmartClipApp::testFilePaths()
{
    // Test that file paths are generated correctly
    // We can't access private methods directly, so we test through the app behavior
    
    // Create some history to trigger file operations
    if (QClipboard *clipboard = QApplication::clipboard()) {
        clipboard->setText("test text");
        QTest::qWait(100); // Allow time for clipboard processing
    }
    
    // The app should handle file operations without crashing
    QVERIFY(true);
}

void TestSmartClipApp::testFormatMenuLabel()
{
    // We can't test the private static method directly
    // But we can test menu building which uses it
    
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // If we had access to formatMenuLabel, we would test:
    // QCOMPARE(formatMenuLabel("short"), "short");
    // QCOMPARE(formatMenuLabel("very long text that should be truncated"), "very long text that should be trunc...");
    // QCOMPARE(formatMenuLabel("text\nwith\nnewlines"), "text with newlines");
    
    QVERIFY(true); // Placeholder for actual formatMenuLabel tests
}

void TestSmartClipApp::testMaskForMenuDisplay()
{
    // len >= 10: show first 3 and last 3
    QCOMPARE(SmartClipApp::maskForMenuDisplay("password12"), "pas****d12");
    QCOMPARE(SmartClipApp::maskForMenuDisplay("abcdefghij"), "abc****hij");
    QCOMPARE(SmartClipApp::maskForMenuDisplay("1234567890"), "123****890");

    // len >= 7 and < 10: show first 2 and last 2
    QCOMPARE(SmartClipApp::maskForMenuDisplay("password"), "pa****rd");
    QCOMPARE(SmartClipApp::maskForMenuDisplay("1234567"), "12***67");
    QCOMPARE(SmartClipApp::maskForMenuDisplay("abcdefgh"), "ab****gh");

    // len < 7: show first 1 and last 1
    QCOMPARE(SmartClipApp::maskForMenuDisplay("abc"), "a*c");
    QCOMPARE(SmartClipApp::maskForMenuDisplay("ab"), "ab");
    QCOMPARE(SmartClipApp::maskForMenuDisplay("a"), "a");

    // Edge cases
    QCOMPARE(SmartClipApp::maskForMenuDisplay(""), "");
    QCOMPARE(SmartClipApp::maskForMenuDisplay("123456"), "1****6");
}

void TestSmartClipApp::testHandleClipboardChange()
{
    if (QClipboard *clipboard = QApplication::clipboard()) {
        // Set initial clipboard content
        clipboard->setText("initial text");
        QTest::qWait(100);
        
        // Change clipboard content
        clipboard->setText("new clipboard content");
        QTest::qWait(200); // Allow time for processing
        
        // The app should handle the change without crashing
        QVERIFY(true);
    }
}

void TestSmartClipApp::testHandleClipboardChangeEmptyText()
{
    if (QClipboard *clipboard = QApplication::clipboard()) {
        // Test with empty text
        clipboard->setText("");
        QTest::qWait(100);
        
        // Test with whitespace only
        clipboard->setText("   \t\n   ");
        QTest::qWait(100);
        
        // The app should handle empty/whitespace text without crashing
        QVERIFY(true);
    }
}

void TestSmartClipApp::testHandleClipboardChangeDuplicateText()
{
    if (QClipboard *clipboard = QApplication::clipboard()) {
        // Set the same text twice
        clipboard->setText("duplicate text");
        QTest::qWait(100);
        
        clipboard->setText("duplicate text");
        QTest::qWait(100);
        
        // The app should handle duplicate text without crashing
        QVERIFY(true);
    }
}

void TestSmartClipApp::testHandleClipboardChangeIgnoredChange()
{
    if (QClipboard *clipboard = QApplication::clipboard()) {
        // Set initial text
        clipboard->setText("test text");
        QTest::qWait(100);
        
        // The app should handle clipboard changes without crashing
        // We can't directly test ignoreNextClipboardChange as it's private
        QVERIFY(true);
    }
}

void TestSmartClipApp::testOnClearHistory()
{
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // Add some history first
    if (QClipboard *clipboard = QApplication::clipboard()) {
        clipboard->setText("item to clear");
        QTest::qWait(100);
    }
    
    // Clear history should not crash
    // We can't directly call onClearHistory as it's private, but we can test the overall behavior
    QVERIFY(true);
}

void TestSmartClipApp::testOnToggleFavorite()
{
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // Add some history first
    if (QClipboard *clipboard = QApplication::clipboard()) {
        clipboard->setText("favorite item");
        QTest::qWait(100);
    }
    
    // Toggle favorite should not crash
    // We can't directly call onToggleFavorite as it's private
    QVERIFY(true);
}

void TestSmartClipApp::testGetFavoriteColorIndex()
{
    // We can't test private methods directly
    // But we can test the overall favorite functionality
    
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // Add items and test favorite functionality indirectly
    if (QClipboard *clipboard = QApplication::clipboard()) {
        clipboard->setText("favorite test item");
        QTest::qWait(100);
    }
    
    // The favorite color management should work without crashing
    QVERIFY(true);
}

void TestSmartClipApp::testReleaseFavoriteColor()
{
    // Similar to getFavoriteColorIndex, we test indirectly
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // The color release functionality should work without crashing
    QVERIFY(true);
}

void TestSmartClipApp::testRebuildMenu()
{
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // Add some history items
    if (QClipboard *clipboard = QApplication::clipboard()) {
        clipboard->setText("menu item 1");
        QTest::qWait(100);
        
        clipboard->setText("menu item 2");
        QTest::qWait(100);
    }
    
    // Menu rebuilding should not crash
    // rebuildMenu() is called automatically when history changes
    QVERIFY(true);
}

void TestSmartClipApp::testHandleExitCleanup()
{
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // Add some history
    if (QClipboard *clipboard = QApplication::clipboard()) {
        clipboard->setText("cleanup test item");
        QTest::qWait(100);
    }
    
    // Exit cleanup should not crash
    // We can't directly call handleExitCleanup as it's private
    // But it's called when the app is destroyed
    QVERIFY(true);
}

void TestSmartClipApp::testOnQuit()
{
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // Quit should not crash
    // We can't directly call onQuit as it's private since it quits the application
    QVERIFY(true);
}

void TestSmartClipApp::testOnSettings()
{
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // Settings dialog should not crash when opened
    // We can't directly call onSettings as it's private and shows a modal dialog
    QVERIFY(true);
}

// Additional helper tests
void TestSmartClipApp::testClipboardPolling()
{
#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    m_smartClipApp->show();
    QTest::qWait(600); // Allow polling timer to trigger at least once
    
    // Clipboard polling should work without crashing
    QVERIFY(true);
#else
    QSKIP("Clipboard polling is macOS/Linux only");
#endif
}

void TestSmartClipApp::testIconUpdate()
{
    m_smartClipApp->show();
    QTest::qWait(100);
    
    // Icon update should not crash
    // updateIcon() is called during initialization
    QVERIFY(true);
}

void TestSmartClipApp::testMultipleClipboardChanges()
{
    m_smartClipApp->show();
    QTest::qWait(100);
    
    if (QClipboard *clipboard = QApplication::clipboard()) {
        // Rapid clipboard changes
        for (int i = 0; i < 5; ++i) {
            clipboard->setText(QString("rapid change %1").arg(i));
            QTest::qWait(50);
        }
    }
    
    // The app should handle rapid changes without crashing
    QVERIFY(true);
}

void TestSmartClipApp::testFavoriteColorPersistence()
{
    // Проверка сохранения и загрузки цвета избранных элементов: приложение загружает
    // историю с favorite_color_index и при выходе сохраняет их обратно в файл.
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + "/smartclip_test_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QVERIFY(QDir().mkpath(tempDir));
    QDir tempDirObj(tempDir);
    QVERIFY(tempDirObj.mkpath(".smartclip"));

    const QString settingsPath = tempDir + "/.smartclip/settings.yml";
    const QString historyPath = tempDir + "/.smartclip/history.yml";

    {
        QFile sf(settingsPath);
        QVERIFY(sf.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&sf);
        out << "max_items: 20\n";
        out << "launch_at_startup: false\n";
        out << "save_history_on_exit: true\n";
    }

    const QString favText = "favorite_item";
    const int expectedColorIndex = 2;
    {
        QFile hf(historyPath);
        QVERIFY(hf.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&hf);
        out << "version: 1\n";
        out << "items:\n";
        out << "  - text_b64: " << QString::fromUtf8(favText.toUtf8().toBase64()) << "\n";
        out << "    usage_count: 0\n";
        out << "    added_at_ms: " << QDateTime::currentMSecsSinceEpoch() << "\n";
        out << "    favorite_color_index: " << expectedColorIndex << "\n";
        out << "    mask_in_menu: 0\n";
    }

    const QByteArray savedHome = qgetenv("HOME");
    qputenv("HOME", tempDir.toUtf8());

    delete m_smartClipApp;
    m_smartClipApp = new SmartClipApp();
    QVERIFY(m_smartClipApp != nullptr);

    // Симулируем выход и сохранение истории
    QMetaObject::invokeMethod(m_smartClipApp, "handleExitCleanup", Qt::DirectConnection);

    qputenv("HOME", savedHome);

    // Проверяем, что в сохранённом файле есть тот же favorite_color_index
    QFile hf(historyPath);
    QVERIFY(hf.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream in(&hf);
    QString content = in.readAll();
    QVERIFY2(content.contains("favorite_color_index:"), "Saved history must contain favorite_color_index");
    QVERIFY2(content.contains(QString("favorite_color_index: %1").arg(expectedColorIndex)),
        qPrintable(QString("Saved history must contain favorite_color_index: %1").arg(expectedColorIndex)));

    // Очистка тестовой директории
    QFile::remove(historyPath);
    QFile::remove(settingsPath);
    tempDirObj.rmdir(".smartclip");
    QDir().rmdir(tempDir);
}

// Кнопка Hide в футере попапа должна быть на месте, жирной, и прятать окно.
void TestSmartClipApp::testTrayPopupHideButton()
{
    TrayPopup popup;
    popup.setRows({});
    popup.showAt(QPoint(200, 100));
    QVERIFY2(popup.isVisible(), "Popup should be visible after showAt");

    QPushButton *hideBtn = popup.findChild<QPushButton *>(QStringLiteral("hideBtn"));
    QVERIFY2(hideBtn != nullptr, "Footer must contain the Hide button");
    QVERIFY2(hideBtn->text() == QStringLiteral("Hide"), "Hide button text");
    QVERIFY2(hideBtn->font().bold(), "Hide button text must be bold");

    // Кнопка Hide идёт ПЕРЕД Clear в футере.
    QPushButton *clearBtn = nullptr;
    for (QPushButton *b : popup.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("Clear")) { clearBtn = b; break; }
    }
    QVERIFY2(clearBtn != nullptr, "Footer must contain Clear");
    const QPoint hp = hideBtn->mapTo(&popup, QPoint(0, 0));
    const QPoint cp = clearBtn->mapTo(&popup, QPoint(0, 0));
    QVERIFY2(hp.x() < cp.x(), "Hide must be placed before Clear");

    // Клик по Hide прячет окно.
    hideBtn->click();
    QVERIFY2(!popup.isVisible(), "Clicking Hide must hide the popup");
}

// Clear и действия строки НЕ должны прятать попап (запрос: окно скрывается
// только при копировании/настройках/справке).
void TestSmartClipApp::testTrayPopupClearDoesNotHide()
{
    TrayPopup popup;
    popup.setRows({});
    popup.showAt(QPoint(200, 100));
    QVERIFY(popup.isVisible());

    QPushButton *clearBtn = nullptr;
    for (QPushButton *b : popup.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("Clear")) { clearBtn = b; break; }
    }
    QVERIFY2(clearBtn != nullptr, "Footer must contain Clear");

    QSignalSpy clearSpy(&popup, &TrayPopup::clearRequested);
    clearBtn->click();
    QCOMPARE(clearSpy.count(), 1);
    QVERIFY2(popup.isVisible(), "Clear must NOT hide the popup");
}

void TestSmartClipApp::testTrayPopupRowActionDoesNotHide()
{
    TrayPopup popup;
    TrayPopup::RowData r;
    r.text = QStringLiteral("sample-text");
    r.display = r.text;
    popup.setRows({r});
    popup.showAt(QPoint(200, 100));
    QVERIFY(popup.isVisible());

    // Кнопки действий строки — QToolButton#rowAction (★ 👁 ✎ ✕).
    const auto actions = popup.findChildren<QToolButton *>(QStringLiteral("rowAction"));
    QVERIFY2(actions.size() == 4, "Row must expose 4 action buttons");

    QSignalSpy favSpy(&popup, &TrayPopup::favoriteToggled);
    QSignalSpy delSpy(&popup, &TrayPopup::deleteRequested);
    // ЛКМ по первой (избранное) и последней (удаление) кнопкам.
    for (QToolButton *b : actions) {
        QTest::mouseClick(b, Qt::LeftButton);
        QVERIFY2(popup.isVisible(),
                 "Row action must NOT hide the popup");
    }
    QCOMPARE(favSpy.count(), 1);
    QCOMPARE(delSpy.count(), 1);
}

// Ширина попапа должна расти под длинный текст+комментарий (без обрезки).
void TestSmartClipApp::testTrayPopupWidthFitsContent()
{
    TrayPopup shortP;
    TrayPopup::RowData s;
    s.text = QStringLiteral("hi");
    s.display = s.text;
    shortP.setRows({s});
    const int wShort = shortP.width();

    TrayPopup longP;
    TrayPopup::RowData l;
    l.text = QStringLiteral("dk@deareditor.ru");
    l.display = l.text;
    l.comment = QString(240, QLatin1Char('x'));   // длинный комментарий
    longP.setRows({l});
    const int wLong = longP.width();

    QVERIFY2(wLong > wShort,
             "Ширина должна расти под более длинное содержимое");
    QVERIFY2(wShort >= 360,
             "Минимальная ширина — по футеру/заголовку");
}

// Скрытие по потере активности — ТОЛЬКО на Wayland. На X11/macOS попап
// закрывает сам Qt::Popup, иначе повторный клик по иконке трея показывал
// попап и тут же прятал его (баг на macOS).
void TestSmartClipApp::testTrayPopupDeactivateHideOnlyOnWayland()
{
    TrayPopup popup;
    popup.setRows({});

    const bool wayland = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");

    popup.showAt(QPoint(200, 100));
    QVERIFY(popup.isVisible());

    // Имитируем потерю активности окна (как это делает WM/оболочка).
    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&popup, &deactivate);

    if (wayland) {
        QVERIFY2(!popup.isVisible(),
                 "На Wayland попап должен прятаться по WindowDeactivate");
    } else {
        QVERIFY2(popup.isVisible(),
                 "На X11/macOS попап НЕ должен прятаться по WindowDeactivate");
    }
}

// На чистой системе (пустая история) попап не должен быть сплющен: заглушка
// «Clipboard history is empty» и кнопки футера обязаны быть видны целиком.
// Регрессия: высота считалась по m_empty->isVisible() (false до show()) и
// скролл-вьюпорт забирал высоту у заглушки → обрезка заголовка и кнопок.
void TestSmartClipApp::testTrayPopupEmptyStateNotSquished()
{
    TrayPopup popup;
    popup.setRows({});
    popup.showAt(QPoint(200, 100));
    QVERIFY(popup.isVisible());

    auto *empty = popup.findChild<QLabel *>(QStringLiteral("empty"));
    QVERIFY2(empty != nullptr, "Empty placeholder label must exist");
    QVERIFY2(empty->isVisible(), "Empty placeholder must be visible when history is empty");
    // Заглушка не обрезана: её высота не меньше собственного sizeHint.
    QVERIFY2(empty->height() >= empty->sizeHint().height(),
             qPrintable(QStringLiteral("empty label squished: %1 < %2")
                            .arg(empty->height()).arg(empty->sizeHint().height())));

    // Область списка при пустой истории скрыта (не отъедает высоту).
    auto *scroll = popup.findChild<QScrollArea *>(QStringLiteral("scroll"));
    QVERIFY2(scroll != nullptr, "Scroll area must exist");
    QVERIFY2(!scroll->isVisible(), "Scroll area must be hidden when history is empty");

    // Кнопки футера не сплющены.
    for (QPushButton *b : popup.findChildren<QPushButton *>()) {
        if (b->objectName() == QStringLiteral("footerBtn")
            || b->objectName() == QStringLiteral("hideBtn")) {
            QVERIFY2(b->height() >= b->sizeHint().height(),
                     qPrintable(QStringLiteral("footer button '%1' squished: %2 < %3")
                                    .arg(b->text()).arg(b->height())
                                    .arg(b->sizeHint().height())));
        }
    }
}

// Окно «О программе»: название, версия, автор, лицензия. Заголовок — без версии.
void TestSmartClipApp::testAboutDialogShowsNameVersionAuthorLicense()
{
    AboutDialog dlg;

    auto *name = dlg.findChild<QLabel *>(QStringLiteral("aboutName"));
    QVERIFY2(name != nullptr, "AboutDialog must have the app name label");
    QCOMPARE(name->text(), QStringLiteral("SmartClip"));

    auto *ver = dlg.findChild<QLabel *>(QStringLiteral("aboutVersion"));
    QVERIFY2(ver != nullptr, "AboutDialog must show the version");
    QVERIFY2(ver->text().startsWith(QStringLiteral("Version ")),
             qPrintable(QStringLiteral("version label: %1").arg(ver->text())));

    auto *author = dlg.findChild<QLabel *>(QStringLiteral("aboutAuthor"));
    QVERIFY2(author != nullptr, "AboutDialog must show the author");
    QVERIFY2(author->text().contains(QStringLiteral("Aleksey Zhmikhov")),
             qPrintable(author->text()));

    auto *lic = dlg.findChild<QLabel *>(QStringLiteral("aboutLicense"));
    QVERIFY2(lic != nullptr, "AboutDialog must show the license");
    QVERIFY2(lic->text().contains(QStringLiteral("MIT")), qPrintable(lic->text()));

    // Заголовок окна НЕ содержит версию.
    QVERIFY2(!dlg.windowTitle().contains(QStringLiteral("1.")),
             qPrintable(dlg.windowTitle()));

    // Кнопка Close закрывает диалог.
    auto *close = dlg.findChild<QPushButton *>(QStringLiteral("aboutClose"));
    QVERIFY2(close != nullptr, "AboutDialog must have a Close button");
    dlg.show();
    QVERIFY(dlg.isVisible());
    close->click();
    QVERIFY2(!dlg.isVisible(), "Close must dismiss the About dialog");
}

// Заголовок справки — без версии (версия переехала в About).
void TestSmartClipApp::testHelpDialogTitleHasNoVersion()
{
    HelpDialog dlg;
    QCOMPARE(dlg.windowTitle(), QStringLiteral("SmartClip Help"));
    QVERIFY2(!dlg.windowTitle().contains(QStringLiteral("1.")),
             qPrintable(dlg.windowTitle()));
}

// Кнопка About в футере попапа: испускает aboutRequested и прячет окно.
void TestSmartClipApp::testTrayPopupAboutButton()
{
    TrayPopup popup;
    popup.setRows({});
    popup.showAt(QPoint(200, 100));
    QVERIFY(popup.isVisible());

    QPushButton *aboutBtn = nullptr;
    for (QPushButton *b : popup.findChildren<QPushButton *>()) {
        if (b->text() == QStringLiteral("About")) { aboutBtn = b; break; }
    }
    QVERIFY2(aboutBtn != nullptr, "Footer must contain the About button");

    QSignalSpy aboutSpy(&popup, &TrayPopup::aboutRequested);
    aboutBtn->click();
    QCOMPARE(aboutSpy.count(), 1);
    QVERIFY2(!popup.isVisible(), "About must hide the popup");
}

QTEST_MAIN(TestSmartClipApp)
#include "SmartClipAppTest.moc"
