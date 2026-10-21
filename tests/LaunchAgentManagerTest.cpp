#include <QtTest/QtTest>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

#if !defined(Q_OS_WIN)
#include <QProcess>
#endif

#if defined(Q_OS_WIN)
 #include <windows.h>
#endif

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
#if defined(Q_OS_WIN)
    // Проверка/уборка автозапуска в реестре HKCU\...\CurrentVersion\Run.
    bool winRunValueExists() const;
    void winRemoveRunValue() const;
#endif
};

QString TestLaunchAgentManager::testAutostartFilePath() const
{
#if defined(Q_OS_MAC)
    return m_testHomeDir + QLatin1String("/Library/LaunchAgents/com.yoshapihoff.smartclip.plist");
#elif defined(Q_OS_LINUX)
    return m_testHomeDir + QLatin1String("/.config/autostart/smartclip.desktop");
#elif defined(Q_OS_WIN)
    return m_launchAgentManager->autostartFilePath();   // ключ реестра (не файл)
#else
    return QString();
#endif
}

#if defined(Q_OS_WIN)
namespace {
constexpr const wchar_t *kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t *kRunValueName = L"SmartClip";
}  // namespace

bool TestLaunchAgentManager::winRunValueExists() const
{
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &hkey)
        != ERROR_SUCCESS)
        return false;
    wchar_t buf[1024] = {0};
    DWORD type = 0, size = sizeof(buf);
    const LONG rc = RegQueryValueExW(hkey, kRunValueName, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(hkey);
    return rc == ERROR_SUCCESS;
}

void TestLaunchAgentManager::winRemoveRunValue() const
{
    HKEY hkey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hkey)
        != ERROR_SUCCESS)
        return;
    RegDeleteValueW(hkey, kRunValueName);
    RegCloseKey(hkey);
}
#endif

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

    // Clean up any potential leftover test entry (file or registry value).
#if defined(Q_OS_WIN)
    winRemoveRunValue();
#else
    QFile::remove(testAutostartFilePath());
#endif
}

void TestLaunchAgentManager::cleanup()
{
    qputenv("HOME", m_originalHome.toUtf8());

#if defined(Q_OS_WIN)
    winRemoveRunValue();
    Q_UNUSED(m_testHomeDir);
#else
    QFile::remove(testAutostartFilePath());

    // Remove temp home tree
    QDir dir(m_testHomeDir);
    dir.removeRecursively();
#endif

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
#elif defined(Q_OS_WIN)
    // Windows: запись создаётся в реестре HKCU\...\Run.
    QVERIFY(winRunValueExists());
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
#elif defined(Q_OS_WIN)
    QVERIFY(winRunValueExists());
#endif

    // Now disable — file must be removed
    m_launchAgentManager->applyLaunchAtStartup(false);

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    QVERIFY(!QFile::exists(testAutostartFilePath()));
#elif defined(Q_OS_WIN)
    QVERIFY(!winRunValueExists());
#endif
}

void TestLaunchAgentManager::testApplyLaunchAtStartupDisabledRemovesFile()
{
    // Manually create the autostart file first, then call disable
    const QString path = testAutostartFilePath();
#if defined(Q_OS_WIN)
    // На Windows сначала включим (запись в реестре), затем выключим.
    m_launchAgentManager->applyLaunchAtStartup(true);
    QVERIFY(winRunValueExists());
#else
    if (!path.isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << "dummy existing autostart entry";
        f.close();
        QVERIFY(QFile::exists(path));
    }
#endif

    m_launchAgentManager->applyLaunchAtStartup(false);

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    QVERIFY(!QFile::exists(path));
#elif defined(Q_OS_WIN)
    QVERIFY(!winRunValueExists());
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
