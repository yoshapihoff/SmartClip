#include "SmartClipApp.h"
#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QIcon>
#include <QDebug>
#include <QMessageBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <QProcess>

#include <algorithm>

#if defined(Q_OS_MAC)
 #include <unistd.h>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
 #include <QStyleHints>
#endif

SmartClipApp::SmartClipApp(QObject *parent)
    : QObject(parent)
{
    loadSettings();
    if (saveHistoryOnExit) {
        loadHistory();
    } else {
        QFile::remove(historyFilePath());
    }
    updateIcon();

    titleAction = new QAction("Select the clip you want to add to your clipboard", this);
    titleAction->setEnabled(false);
    
    settingsAction = new QAction("Settings", this);
    connect(settingsAction, &QAction::triggered, this, &SmartClipApp::onSettings);

    quitAction = new QAction("Quit", this);
    connect(quitAction, &QAction::triggered, this, &SmartClipApp::onQuit);

    rebuildMenu();

    trayIcon.setContextMenu(&trayMenu);
    trayIcon.setToolTip("SmartClip");

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (exitHandled) {
            return;
        }
        exitHandled = true;

        if (saveHistoryOnExit) {
            if (historyDirty) {
                saveHistory();
            }
        } else {
            QFile::remove(historyFilePath());
        }
    });

    if (QClipboard *clipboard = QApplication::clipboard()) {
        connect(clipboard, &QClipboard::dataChanged, this, &SmartClipApp::onClipboardChanged);
        connect(clipboard, &QClipboard::changed, this, [this](QClipboard::Mode mode) {
            if (mode == QClipboard::Clipboard) {
                onClipboardChanged();
            }
        });
    }

#if defined(Q_OS_MAC)
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
}

void SmartClipApp::show()
{
    trayIcon.show();
}

void SmartClipApp::onClipboardChanged()
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return;
    }

    const QString text = clipboard->text(QClipboard::Clipboard);
    qDebug() << "Clipboard changed: " << text;
    addToHistory(text);
}

void SmartClipApp::pollClipboard()
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return;
    }

    const QString text = clipboard->text(QClipboard::Clipboard);
    addToHistory(text);
}

void SmartClipApp::addToHistory(const QString &text)
{
    if (ignoreNextClipboardChange) {
        ignoreNextClipboardChange = false;
        lastClipboardText = text;
        return;
    }

    if (text.trimmed().isEmpty()) {
        return;
    }

    if (text == lastClipboardText) {
        return;
    }
    lastClipboardText = text;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    auto it = std::find_if(history.begin(), history.end(), [&text](const HistoryItem &item) {
        return item.text == text;
    });

    if (it == history.end()) {
        HistoryItem item;
        item.text = text;
        item.usageCount = 0;
        item.addedAtMs = nowMs;
        history.push_back(item);
    } else {
        it->addedAtMs = nowMs;
    }

    trimHistoryToMaxItems();
    historyDirty = true;
    rebuildMenu();
}

void SmartClipApp::trimHistoryToMaxItems()
{
    while (history.size() > maxItems) {
        int oldestIndex = 0;
        qint64 oldestTs = history.at(0).addedAtMs;
        for (int i = 1; i < history.size(); ++i) {
            const qint64 ts = history.at(i).addedAtMs;
            if (ts < oldestTs) {
                oldestTs = ts;
                oldestIndex = i;
            }
        }
        history.removeAt(oldestIndex);
    }
}

void SmartClipApp::onSettings()
{
    QDialog dialog;
    dialog.setWindowTitle("Settings");

    QFormLayout *layout = new QFormLayout(&dialog);

    QSpinBox *maxItemsSpin = new QSpinBox(&dialog);
    maxItemsSpin->setMinimum(1);
    maxItemsSpin->setMaximum(1000);
    maxItemsSpin->setValue(maxItems);

    QCheckBox *launchAtStartupCheck = new QCheckBox(&dialog);
    launchAtStartupCheck->setChecked(launchAtStartup);

    QCheckBox *saveHistoryOnExitCheck = new QCheckBox(&dialog);
    saveHistoryOnExitCheck->setChecked(saveHistoryOnExit);

    layout->addRow("History size", maxItemsSpin);
    layout->addRow("Launch at startup", launchAtStartupCheck);
    layout->addRow("Save history on exit", saveHistoryOnExitCheck);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int newMaxItems = maxItemsSpin->value();
    const bool newLaunchAtStartup = launchAtStartupCheck->isChecked();
    const bool newSaveHistoryOnExit = saveHistoryOnExitCheck->isChecked();

    bool settingsChanged = false;
    if (maxItems != newMaxItems) {
        maxItems = newMaxItems;
        trimHistoryToMaxItems();
        rebuildMenu();
        historyDirty = true;
        settingsChanged = true;
    }

    if (launchAtStartup != newLaunchAtStartup) {
        launchAtStartup = newLaunchAtStartup;
        applyLaunchAtStartup(launchAtStartup);
        settingsChanged = true;
    }

    if (saveHistoryOnExit != newSaveHistoryOnExit) {
        saveHistoryOnExit = newSaveHistoryOnExit;
        if (!saveHistoryOnExit) {
            QFile::remove(historyFilePath());
        }
        settingsChanged = true;
    }

    if (settingsChanged) {
        saveSettings();
    }
}

void SmartClipApp::onQuit()
{
    if (!exitHandled) {
        exitHandled = true;
        if (saveHistoryOnExit) {
            if (historyDirty) {
                saveHistory();
            }
        } else {
            QFile::remove(historyFilePath());
        }
    }

    qApp->quit();
}

void SmartClipApp::rebuildMenu()
{
    trayMenu.clear();

    if (titleAction) {
        trayMenu.addAction(titleAction);
    }

    if (!history.isEmpty()) {
        trayMenu.addSeparator();
    }

    std::sort(history.begin(), history.end(), [](const HistoryItem &a, const HistoryItem &b) {
        if (a.usageCount != b.usageCount) {
            return a.usageCount > b.usageCount;
        }
        if (a.addedAtMs != b.addedAtMs) {
            return a.addedAtMs > b.addedAtMs;
        }
        return a.text < b.text;
    });

    for (int i = 0; i < history.size(); ++i) {
        const QString text = history.at(i).text;
        QAction *action = trayMenu.addAction(formatMenuLabel(text));
        connect(action, &QAction::triggered, this, [this, text]() {
            auto it = std::find_if(history.begin(), history.end(), [&text](const HistoryItem &item) {
                return item.text == text;
            });
            if (it != history.end()) {
                it->usageCount += 1;
                historyDirty = true;
            }

            if (QClipboard *clipboard = QApplication::clipboard()) {
                ignoreNextClipboardChange = true;
                clipboard->setText(text, QClipboard::Clipboard);
            }

            trimHistoryToMaxItems();
            rebuildMenu();
        });
    }

    if (!history.isEmpty()) {
        trayMenu.addSeparator();
    }

    if (settingsAction) {
        trayMenu.addAction(settingsAction);
    }
    if (quitAction) {
        trayMenu.addAction(quitAction);
    }
}

QString SmartClipApp::settingsFilePath() const
{
    return QDir::homePath() + QLatin1String("/.smartclip/settings.yml");
}

QString SmartClipApp::historyFilePath() const
{
    return QDir::homePath() + QLatin1String("/.smartclip/history.yml");
}

void SmartClipApp::loadSettings()
{
    const QString path = settingsFilePath();
    const QFileInfo fi(path);

    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    QFile f(path);
    if (!f.exists()) {
        saveSettings();
        return;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QRegularExpression re(QLatin1String("^\\s*max_items\\s*:\\s*(\\d+)\\s*$"));
        const QRegularExpressionMatch m = re.match(line);
        if (m.hasMatch()) {
            bool ok = false;
            const int v = m.captured(1).toInt(&ok);
            if (ok && v > 0) {
                maxItems = v;
            }
        }

        {
            const QRegularExpression re2(QLatin1String("^\\s*launch_at_startup\\s*:\\s*(true|false)\\s*$"));
            const QRegularExpressionMatch m2 = re2.match(line);
            if (m2.hasMatch()) {
                launchAtStartup = (m2.captured(1) == QLatin1String("true"));
            }
        }
        {
            const QRegularExpression re3(QLatin1String("^\\s*save_history_on_exit\\s*:\\s*(true|false)\\s*$"));
            const QRegularExpressionMatch m3 = re3.match(line);
            if (m3.hasMatch()) {
                saveHistoryOnExit = (m3.captured(1) == QLatin1String("true"));
            }
        }
    }

    applyLaunchAtStartup(launchAtStartup);
}

void SmartClipApp::saveSettings() const
{
    const QString path = settingsFilePath();
    const QFileInfo fi(path);
    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    QTextStream out(&f);
    out << "max_items: " << maxItems << "\n";
    out << "launch_at_startup: " << (launchAtStartup ? "true" : "false") << "\n";
    out << "save_history_on_exit: " << (saveHistoryOnExit ? "true" : "false") << "\n";
}

QString SmartClipApp::launchAgentPlistPath() const
{
#if defined(Q_OS_MAC)
    return QDir::homePath() + QLatin1String("/Library/LaunchAgents/com.yoshapihoff.smartclip.plist");
#else
    return QString();
#endif
}

void SmartClipApp::applyLaunchAtStartup(bool enabled)
{
#if defined(Q_OS_MAC)
    const QString plistPath = launchAgentPlistPath();
    const QFileInfo fi(plistPath);
    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    if (!enabled) {
        QProcess::execute("/bin/launchctl", {"unload", plistPath});
        QFile::remove(plistPath);
        return;
    }

    QFile f(plistPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    const QString program = QCoreApplication::applicationFilePath();

    QTextStream out(&f);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    out << "<plist version=\"1.0\">\n";
    out << "<dict>\n";
    out << "  <key>Label</key>\n";
    out << "  <string>com.yoshapihoff.smartclip</string>\n";
    out << "  <key>ProgramArguments</key>\n";
    out << "  <array>\n";
    out << "    <string>" << program << "</string>\n";
    out << "  </array>\n";
    out << "  <key>RunAtLoad</key>\n";
    out << "  <true/>\n";
    out << "</dict>\n";
    out << "</plist>\n";

    QProcess::execute("/bin/launchctl", {"unload", plistPath});
    QProcess::execute("/bin/launchctl", {"load", plistPath});
#else
    (void)enabled;
#endif
}

void SmartClipApp::loadHistory()
{
    const QString path = historyFilePath();
    QFile f(path);
    if (!f.exists()) {
        return;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QVector<HistoryItem> loaded;
    HistoryItem current;
    bool inItem = false;

    auto parseKeyValue = [&current](const QString &line) {
        const int idx = line.indexOf(QLatin1Char(':'));
        if (idx <= 0) {
            return;
        }

        const QString key = line.left(idx).trimmed();
        const QString val = line.mid(idx + 1).trimmed();

        if (key == QLatin1String("text_b64")) {
            current.text = QString::fromUtf8(QByteArray::fromBase64(val.toUtf8()));
        } else if (key == QLatin1String("usage_count")) {
            bool ok = false;
            const int v = val.toInt(&ok);
            if (ok && v >= 0) {
                current.usageCount = v;
            }
        } else if (key == QLatin1String("added_at_ms")) {
            bool ok = false;
            const qint64 v = val.toLongLong(&ok);
            if (ok && v >= 0) {
                current.addedAtMs = v;
            }
        }
    };

    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QString t = line.trimmed();

        if (t.startsWith(QLatin1Char('-'))) {
            if (inItem && !current.text.isEmpty()) {
                loaded.push_back(current);
            }
            current = HistoryItem{};
            inItem = true;

            const QString rest = t.mid(1).trimmed();
            if (!rest.isEmpty()) {
                parseKeyValue(rest);
            }
            continue;
        }

        if (!inItem) {
            continue;
        }

        parseKeyValue(t);
    }

    if (inItem && !current.text.isEmpty()) {
        loaded.push_back(current);
    }

    history = loaded;
    trimHistoryToMaxItems();
}

void SmartClipApp::saveHistory() const
{
    const QString path = historyFilePath();
    const QFileInfo fi(path);
    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    QTextStream out(&f);
    out << "version: 1\n";
    out << "items:\n";
    for (const HistoryItem &item : history) {
        const QByteArray b64 = item.text.toUtf8().toBase64();
        out << "  - text_b64: " << b64 << "\n";
        out << "    usage_count: " << item.usageCount << "\n";
        out << "    added_at_ms: " << item.addedAtMs << "\n";
    }
}

QString SmartClipApp::formatMenuLabel(const QString &text)
{
    QString s = text;
    s.replace('\n', ' ');
    s.replace('\r', ' ');
    s = s.simplified();

    const int maxLen = 60;
    if (s.size() > maxLen) {
        s = s.left(maxLen - 1) + QChar(0x2026);
    }

    return s;
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
    bool darkMode = false;

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QStyleHints *hints = qApp->styleHints()) {
        darkMode = (hints->colorScheme() == Qt::ColorScheme::Dark);
    }
#endif

    QIcon icon = darkMode
        ? QIcon(":/icons/tray_white.svg")
        : QIcon(":/icons/tray_black.svg");

    Q_ASSERT(!icon.isNull());
    trayIcon.setIcon(icon);
#endif
}