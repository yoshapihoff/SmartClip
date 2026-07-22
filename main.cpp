#include <QApplication>
#include <QSystemTrayIcon>
#include <QMessageBox>

#include "SmartClipApp.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    // Не показывать иконку в Dock (macOS) и не завершать приложение
    // при закрытии окон, т.к. оно живёт в system tray
    app.setQuitOnLastWindowClosed(false);
#endif

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(
            nullptr,
            "Tray not available",
            "System tray is not available on this system."
        );
        return 1;
    }

    SmartClipApp tray;
    tray.show();

    return app.exec();
}
