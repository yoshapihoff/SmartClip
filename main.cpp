#include <QApplication>
#include <QSystemTrayIcon>
#include <QMessageBox>
#include <QProcessEnvironment>

#include "SmartClipApp.h"

int main(int argc, char *argv[])
{
#if defined(Q_OS_LINUX)
    // ВАЖНО (02.10.2026): Qt на нативном Wayland отдаёт буфер обмена ТОЛЬКО
    // когда у приложения есть фокус клавиатуры. Трей-приложение фокуса не
    // имеет → QClipboard::text() всегда пуст, и новые копии не попадают в
    // историю. Под X11/XWayland (xcb) буфер читается нормально.
    // Поэтому на Wayland форсируем XWayland-бэкенд, если пользователь не
    // задал QT_QPA_PLATFORM явно.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")
            && !QProcessEnvironment::systemEnvironment()
                    .value("WAYLAND_DISPLAY").isEmpty()
            && !qEnvironmentVariableIsSet("SMARTCLIP_NO_XCB")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

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
