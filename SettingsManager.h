#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>

class SettingsManager final : public QObject
{
    Q_OBJECT

signals:
    void settingsChanged();

public:
    explicit SettingsManager(QObject *parent = nullptr);
    ~SettingsManager() = default;

    int maxItems() const;
    bool launchAtStartup() const;
    bool saveHistoryOnExit() const;

    void setMaxItems(int maxItems);
    void setLaunchAtStartup(bool enabled);
    void setSaveHistoryOnExit(bool enabled);

    // ─────────────────────────── сеть (MQTT-синхронизация) ───────────────────────────
    bool syncEnabled() const { return m_syncEnabled; }
    QString brokerHost() const { return m_brokerHost; }
    int brokerPort() const { return m_brokerPort; }
    bool useTls() const { return m_useTls; }
    QString brokerUser() const { return m_brokerUser; }
    QString brokerPassword() const { return m_brokerPassword; }   // расшифрованный
    QString syncEncryptionPassword() const { return m_encPassword; } // расшифрованный
    QString syncRole() const { return m_syncRole; }   // "master" | "slave"
    QString syncRoom() const { return m_syncRoom; }
    bool isMaster() const { return m_syncRole != QLatin1String("slave"); }

    void setSyncEnabled(bool on);
    void setBrokerHost(const QString &host);
    void setBrokerPort(int port);
    void setUseTls(bool on);
    void setBrokerUser(const QString &user);
    void setBrokerPassword(const QString &password);
    void setSyncEncryptionPassword(const QString &password);
    void setSyncRole(const QString &role);
    void setSyncRoom(const QString &room);

    /** Ключ шифрования для паролей в settings.yml (из keyring). */
    void setSecretKey(const QByteArray &key) { m_secretKey = key; }

    /** Загрузить/сохранить сетевые поля одним вызовом (из диалога настроек). */
    void setNetworkSettings(bool enabled, const QString &host, int port, bool tls,
                            const QString &user, const QString &password,
                            const QString &encPassword, const QString &role,
                            const QString &room);

    void loadSettings(const QString &filePath);
    void saveSettings(const QString &filePath) const;
    void saveCurrentSettings();

private:
    int m_maxItems = 32;
    bool m_launchAtStartup = false;
    bool m_saveHistoryOnExit = true;

    bool m_syncEnabled = false;
    QString m_brokerHost;
    int m_brokerPort = 1883;
    bool m_useTls = false;
    QString m_brokerUser;
    QString m_brokerPassword;
    QString m_encPassword;
    QString m_syncRole = QStringLiteral("master");
    QString m_syncRoom = QStringLiteral("smartclip");

    QByteArray m_secretKey;   // AES-256-GCM для паролей в файле
    QString m_currentFilePath;
};
