#include <QApplication>
#include <QSystemTrayIcon>
#include <QMessageBox>

#include "SmartClipApp.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

#if defined(Q_OS_MAC)
    // Не показывать иконку в Dock
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
