#include <QtTest/QtTest>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

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
    void testApplyLaunchAtStartupDisabledRemovesFile();
    void testApplyLaunchAtStartupCrossPlatform();

private:
    LaunchAgentManager *m_launchAgentManager;
    QString m_testHomeDir;
    QString m_originalHome;

    // Platform-specific helpers
    QString testAutostartFilePath() const;
};

QString TestLaunchAgentManager::testAutostartFilePath() const
{
#if defined(Q_OS_MAC)
    return m_testHomeDir + QLatin1String("/Library/LaunchAgents/com.yoshapihoff.smartclip.plist");
#elif defined(Q_OS_LINUX)
    return m_testHomeDir + QLatin1String("/.config/autostart/smartclip.desktop");
#else
    return QString();
#endif
}

void TestLaunchAgentManager::init()
{
    m_originalHome = qgetenv("HOME");

    // Create a temporary home directory so tests don't affect the real system
    m_testHomeDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                    + QLatin1String("/smartclip_test_home_")
                    + QString::number(QDateTime::currentMSecsSinceEpoch());

    QVERIFY(QDir().mkpath(m_testHomeDir));
    qputenv("HOME", m_testHomeDir.toUtf8());

    m_launchAgentManager = new LaunchAgentManager();

    // Clean up any potential leftover test file
    QFile::remove(testAutostartFilePath());
}

void TestLaunchAgentManager::cleanup()
{
    qputenv("HOME", m_originalHome.toUtf8());

    QFile::remove(testAutostartFilePath());

    // Remove temp home tree
    QDir dir(m_testHomeDir);
    dir.removeRecursively();

    delete m_launchAgentManager;
}

void TestLaunchAgentManager::testInitialization()
{
    QVERIFY(m_launchAgentManager != nullptr);
}

void TestLaunchAgentManager::testApplyLaunchAtStartupEnabled()
{
    m_launchAgentManager->applyLaunchAtStartup(true);

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    // On macOS and Linux the autostart file must be created
    const QString path = testAutostartFilePath();
    QVERIFY(QFile::exists(path));

    // Check that the file references the application executable
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY(content.contains(QCoreApplication::applicationFilePath().toUtf8()));
#else
    // Other platforms: no-op, no file created
    QVERIFY(!QFile::exists(testAutostartFilePath()) || testAutostartFilePath().isEmpty());
#endif
}

void TestLaunchAgentManager::testApplyLaunchAtStartupDisabled()
{
    // Enable first to create the file
    m_launchAgentManager->applyLaunchAtStartup(true);

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    QVERIFY(QFile::exists(testAutostartFilePath()));
#endif

    // Now disable — file must be removed
    m_launchAgentManager->applyLaunchAtStartup(false);

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    QVERIFY(!QFile::exists(testAutostartFilePath()));
#endif
}

void TestLaunchAgentManager::testApplyLaunchAtStartupDisabledRemovesFile()
{
    // Manually create the autostart file first, then call disable
    const QString path = testAutostartFilePath();
    if (!path.isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "dummy existing autostart entry";
        f.close();
        QVERIFY(QFile::exists(path));
    }

    m_launchAgentManager->applyLaunchAtStartup(false);

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    QVERIFY(!QFile::exists(path));
#endif
}

void TestLaunchAgentManager::testApplyLaunchAtStartupCrossPlatform()
{
    // Toggle enable/disable in sequence — must not crash on any platform
    m_launchAgentManager->applyLaunchAtStartup(true);
    m_launchAgentManager->applyLaunchAtStartup(false);
    m_launchAgentManager->applyLaunchAtStartup(true);
    m_launchAgentManager->applyLaunchAtStartup(false);

    // No crash = pass
    QVERIFY(true);
}


QTEST_MAIN(TestLaunchAgentManager)
#include "LaunchAgentManagerTest.moc"
