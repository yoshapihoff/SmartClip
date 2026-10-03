#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>

#ifdef SMARTCLIP_HAVE_MQTT
class QMqttClient;
#endif

// ─────────────────────────────────────────────────────────────────────────
// MqttClient — тонкая обёртка над QMqttClient(Qt6::Mqtt).
// Если модуль MQTT не собран (SMARTCLIP_HAVE_MQTT не определён), класс
// существует, но методы — no-op: приложение собирается и работает без синка.
// ─────────────────────────────────────────────────────────────────────────
class MqttClient final : public QObject
{
    Q_OBJECT

public:
    explicit MqttClient(QObject *parent = nullptr);
    ~MqttClient() override;

    void configure(const QString &host, int port, bool tls,
                   const QString &user, const QString &password);
    /** Подключиться. Если уже подключены — ничего не делает. */
    void connectToBroker();
    void disconnectFromBroker();
    bool isConnected() const;

    /** Публикация сырого payload (шифрование — на стороне SyncManager). */
    void publish(const QString &topic, const QByteArray &payload);
    /** Подписка на топик. Переподписка происходит автоматически при reconnect. */
    void subscribe(const QString &topic);

    static bool available();

signals:
    void connected();
    void disconnected();
    void messageReceived(const QString &topic, const QByteArray &payload);
    void errorOccurred(const QString &error);

private slots:
    void onConnected();
    void onDisconnected();

private:
    QString m_host;
    int m_port = 1883;
    bool m_tls = false;
    QString m_user;
    QString m_password;
    QString m_subscribeTopic;   // на что подписаны (переподписываемся при connect)

#ifdef SMARTCLIP_HAVE_MQTT
    QMqttClient *m_client = nullptr;
#endif
};
