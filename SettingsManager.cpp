#include "SettingsManager.h"
#include "Crypto.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>

namespace {

// Пароли в settings.yml не пишем открыто: шифруем AES-256-GCM ключом из
// системного хранилища (тот же keyring, что и история). Формат значения:
//   v2:<base64(nonce||ct||tag)>  — зашифровано
//   p:<base64(utf8)>             — открыто (ключ недоступен)
QString encodeSecret(const QString &plain, const QByteArray &key)
{
    if (plain.isEmpty())
        return QString();
    if (key.size() == 32 && Crypto::available()) {
        const QByteArray blob = Crypto::encrypt(plain.toUtf8(), key);
        if (!blob.isEmpty())
            return QLatin1String("v2:") + QString::fromLatin1(blob.toBase64());
    }
    return QLatin1String("p:")
           + QString::fromLatin1(plain.toUtf8().toBase64());
}

QString decodeSecret(const QString &val, const QByteArray &key)
{
    if (val.startsWith(QLatin1String("v2:"))) {
        const QByteArray blob = QByteArray::fromBase64(val.mid(3).toUtf8());
        bool ok = false;
        const QByteArray plain = Crypto::decrypt(blob, key, &ok);
        return ok ? QString::fromUtf8(plain) : QString();
    }
    if (val.startsWith(QLatin1String("p:")))
        return QString::fromUtf8(QByteArray::fromBase64(val.mid(2).toUtf8()));
    // обратная совместимость: если сохранён открытый base64 без префикса
    return QString::fromUtf8(QByteArray::fromBase64(val.toUtf8()));
}

QString firstMatch(const QString &line, const QString &key)
{
    const QRegularExpression re(
        QLatin1String("^\\s*") + QRegularExpression::escape(key)
        + QLatin1String("\\s*:\\s*(.*?)\\s*$"));
    const QRegularExpressionMatch m = re.match(line);
    return m.hasMatch() ? m.captured(1) : QString();
}

}  // namespace

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
}

int SettingsManager::maxItems() const { return m_maxItems; }
bool SettingsManager::launchAtStartup() const { return m_launchAtStartup; }
bool SettingsManager::saveHistoryOnExit() const { return m_saveHistoryOnExit; }

void SettingsManager::setMaxItems(int maxItems)
{
    if (m_maxItems != maxItems) {
        m_maxItems = maxItems;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setLaunchAtStartup(bool enabled)
{
    if (m_launchAtStartup != enabled) {
        m_launchAtStartup = enabled;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setSaveHistoryOnExit(bool enabled)
{
    if (m_saveHistoryOnExit != enabled) {
        m_saveHistoryOnExit = enabled;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setSyncEnabled(bool on)
{
    if (m_syncEnabled != on) {
        m_syncEnabled = on;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setBrokerHost(const QString &host)
{
    if (m_brokerHost != host) {
        m_brokerHost = host;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setBrokerPort(int port)
{
    if (m_brokerPort != port) {
        m_brokerPort = port;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setUseTls(bool on)
{
    if (m_useTls != on) {
        m_useTls = on;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setBrokerUser(const QString &user)
{
    if (m_brokerUser != user) {
        m_brokerUser = user;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setBrokerPassword(const QString &password)
{
    if (m_brokerPassword != password) {
        m_brokerPassword = password;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setSyncEncryptionPassword(const QString &password)
{
    if (m_encPassword != password) {
        m_encPassword = password;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setSyncRole(const QString &role)
{
    const QString r = (role == QLatin1String("slave")) ? QStringLiteral("slave")
                                                       : QStringLiteral("master");
    if (m_syncRole != r) {
        m_syncRole = r;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setSyncRoom(const QString &room)
{
    if (m_syncRoom != room) {
        m_syncRoom = room;
        saveCurrentSettings();
        emit settingsChanged();
    }
}

void SettingsManager::setNetworkSettings(bool enabled, const QString &host,
                                         int port, bool tls, const QString &user,
                                         const QString &password,
                                         const QString &encPassword,
                                         const QString &role,
                                         const QString &room)
{
    m_syncEnabled = enabled;
    m_brokerHost = host.trimmed();
    m_brokerPort = (port > 0 && port < 65536) ? port : 1883;
    m_useTls = tls;
    m_brokerUser = user.trimmed();
    m_brokerPassword = password;
    m_encPassword = encPassword;
    m_syncRole = (role == QLatin1String("slave")) ? QStringLiteral("slave")
                                                  : QStringLiteral("master");
    m_syncRoom = room.trimmed().isEmpty() ? QStringLiteral("smartclip")
                                          : room.trimmed();
    saveCurrentSettings();
    emit settingsChanged();
}

void SettingsManager::loadSettings(const QString &filePath)
{
    m_currentFilePath = filePath;

    const QFileInfo fi(filePath);
    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    QFile f(filePath);
    if (!f.exists()) {
        saveSettings(filePath);
        return;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('#')))
            continue;

        {
            const QString v = firstMatch(t, QStringLiteral("max_items"));
            if (!v.isEmpty()) {
                bool ok = false;
                const int n = v.toInt(&ok);
                if (ok && n > 0)
                    m_maxItems = n;
                continue;
            }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("launch_at_startup"));
            if (!v.isEmpty()) {
                m_launchAtStartup = (v == QLatin1String("true"));
                continue;
            }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("save_history_on_exit"));
            if (!v.isEmpty()) {
                m_saveHistoryOnExit = (v == QLatin1String("true"));
                continue;
            }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("sync_enabled"));
            if (!v.isEmpty()) {
                m_syncEnabled = (v == QLatin1String("true"));
                continue;
            }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("broker_host"));
            if (!v.isEmpty()) { m_brokerHost = v; continue; }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("broker_port"));
            if (!v.isEmpty()) {
                bool ok = false;
                const int n = v.toInt(&ok);
                if (ok && n > 0 && n < 65536) m_brokerPort = n;
                continue;
            }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("use_tls"));
            if (!v.isEmpty()) { m_useTls = (v == QLatin1String("true")); continue; }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("broker_user"));
            if (!v.isEmpty()) { m_brokerUser = v; continue; }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("broker_password"));
            if (!v.isEmpty()) { m_brokerPassword = decodeSecret(v, m_secretKey); continue; }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("sync_enc_password"));
            if (!v.isEmpty()) { m_encPassword = decodeSecret(v, m_secretKey); continue; }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("sync_role"));
            if (!v.isEmpty()) {
                // Присваиваем НАПРЯМУЮ, без setSyncRole(): тот вызывает
                // saveCurrentSettings() → saveSettings(), который перезаписал
                // бы файл, пока мы его ещё читаем (и sync_room ниже терялся).
                m_syncRole = (v == QLatin1String("slave")) ? QStringLiteral("slave")
                                                            : QStringLiteral("master");
                continue;
            }
        }
        {
            const QString v = firstMatch(t, QStringLiteral("sync_room"));
            if (!v.isEmpty()) { m_syncRoom = v; continue; }
        }
    }
}

void SettingsManager::saveSettings(const QString &filePath) const
{
    const QFileInfo fi(filePath);
    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    QTextStream out(&f);
    out << "max_items: " << m_maxItems << "\n";
    out << "launch_at_startup: " << (m_launchAtStartup ? "true" : "false") << "\n";
    out << "save_history_on_exit: " << (m_saveHistoryOnExit ? "true" : "false") << "\n";
    out << "\n# ── Network sync (MQTT) ──\n";
    out << "sync_enabled: " << (m_syncEnabled ? "true" : "false") << "\n";
    out << "broker_host: " << m_brokerHost << "\n";
    out << "broker_port: " << m_brokerPort << "\n";
    out << "use_tls: " << (m_useTls ? "true" : "false") << "\n";
    out << "broker_user: " << m_brokerUser << "\n";
    out << "broker_password: " << encodeSecret(m_brokerPassword, m_secretKey) << "\n";
    out << "sync_enc_password: " << encodeSecret(m_encPassword, m_secretKey) << "\n";
    out << "sync_role: " << m_syncRole << "\n";
    out << "sync_room: " << m_syncRoom << "\n";

    // settings.yml содержит учётку брокера и общий пароль синка (при
    // неработающем keyring — открытым base64). Закрываем права до владельца.
    out.flush();
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
}

void SettingsManager::saveCurrentSettings()
{
    if (!m_currentFilePath.isEmpty()) {
        saveSettings(m_currentFilePath);
    }
}
