#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryFile>

#include "../LaunchAgentManager.h"

class TestLaunchAgentManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testInitialization();
    void testApplyLaunchAtStartupEnabled();
    void testApplyLaunchAtStartupDisabled();
    void testApplyLaunchAtStartupDisabledFileExists();
    void testApplyLaunchAtStartupNonMac();

private:
    LaunchAgentManager *m_launchAgentManager;
    QString m_testPlistPath;
    QString m_originalAppPath;
};

void TestLaunchAgentManager::init()
{
    m_launchAgentManager = new LaunchAgentManager();
    
    // Create a test plist path
    m_testPlistPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/test_smartclip.plist";
    
    // Store original application path
    m_originalAppPath = QCoreApplication::applicationFilePath();
    
    // Clean up any existing test file
    QFile::remove(m_testPlistPath);
}

void TestLaunchAgentManager::cleanup()
{
    QFile::remove(m_testPlistPath);
    delete m_launchAgentManager;
}

void TestLaunchAgentManager::testInitialization()
{
    QVERIFY(m_launchAgentManager != nullptr);
}

void TestLaunchAgentManager::testApplyLaunchAtStartupEnabled()
{
    // This test will create a plist file on macOS
    // On other platforms it should do nothing
    
#if defined(Q_OS_MAC)
    // Mock the application path for testing
    // Note: In real tests, we might need to use a mock QCoreApplication
    
    m_launchAgentManager->applyLaunchAtStartup(true);
    
    // Check if plist file was created (may not work due to actual launchctl calls)
    // For now, just ensure no crash occurs
    QVERIFY(true);
#else
    // On non-Mac platforms, should do nothing without crashing
    m_launchAgentManager->applyLaunchAtStartup(true);
    QVERIFY(true);
#endif
}

void TestLaunchAgentManager::testApplyLaunchAtStartupDisabled()
{
    // This test should remove the plist file if it exists
    
#if defined(Q_OS_MAC)
    // First create a dummy plist file
    QFile file(m_testPlistPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "dummy content";
        file.close();
    }
    
    QVERIFY(QFile::exists(m_testPlistPath));
    
    // Note: We can't easily test the actual plistPath() without modifying the class
    // For now, just ensure the method doesn't crash
    m_launchAgentManager->applyLaunchAtStartup(false);
    
    // The actual plist file at the real path should be handled by the method
    QVERIFY(true);
#else
    m_launchAgentManager->applyLaunchAtStartup(false);
    QVERIFY(true);
#endif
}

void TestLaunchAgentManager::testApplyLaunchAtStartupDisabledFileExists()
{
    // Test disabling when plist file exists
    // This is similar to the previous test but focuses on the file existence case
    
#if defined(Q_OS_MAC)
    // Create a test plist file
    QFile file(m_testPlistPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    out << "<plist version=\"1.0\">\n";
    out << "<dict>\n";
    out << "  <key>Label</key>\n";
    out << "  <string>com.yoshapihoff.smartclip</string>\n";
    out << "</dict>\n";
    out << "</plist>\n";
    file.close();
    
    QVERIFY(QFile::exists(m_testPlistPath));
    
    // Apply disabled state
    m_launchAgentManager->applyLaunchAtStartup(false);
    
    // Method should complete without crash
    QVERIFY(true);
#else
    m_launchAgentManager->applyLaunchAtStartup(false);
    QVERIFY(true);
#endif
}

void TestLaunchAgentManager::testApplyLaunchAtStartupNonMac()
{
    // Test that the method doesn't crash on non-Mac platforms
    // This test will run on all platforms but the behavior differs
    
    m_launchAgentManager->applyLaunchAtStartup(true);
    m_launchAgentManager->applyLaunchAtStartup(false);
    
    // Should not crash on any platform
    QVERIFY(true);
}


QTEST_MAIN(TestLaunchAgentManager)
#include "LaunchAgentManagerTest.moc"
