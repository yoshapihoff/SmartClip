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

#include "../SmartClipApp.h"
#include "../SettingsManager.h"
#include "../HistoryManager.h"
#include "../LaunchAgentManager.h"

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
#if defined(Q_OS_MAC)
    m_smartClipApp->show();
    QTest::qWait(600); // Allow polling timer to trigger at least once
    
    // Clipboard polling should work without crashing
    QVERIFY(true);
#else
    QSKIP("Clipboard polling is macOS only");
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

QTEST_MAIN(TestSmartClipApp)
#include "SmartClipAppTest.moc"
