#include "MqttClient.h"
#include <QDebug>

#ifdef SMARTCLIP_HAVE_MQTT
#include <QtMqtt/QMqttClient>
#include <QtMqtt/QMqttTopicName>
#include <QtMqtt/QMqttSubscription>
#include <QtMqtt/QMqttTopicFilter>
#ifndef QT_NO_SSL
#include <QtNetwork/QSslConfiguration>
#endif
#endif

MqttClient::MqttClient(QObject *parent)
    : QObject(parent)
{
}

MqttClient::~MqttClient() = default;

bool MqttClient::available()
{
#ifdef SMARTCLIP_HAVE_MQTT
    return true;
#else
    return false;
#endif
}

void MqttClient::configure(const QString &host, int port, bool tls,
                           const QString &user, const QString &password)
{
    m_host = host;
    m_port = port > 0 ? port : 1883;
    m_tls = tls;
    m_user = user;
    m_password = password;

#ifdef SMARTCLIP_HAVE_MQTT
    if (!m_client)
        m_client = new QMqttClient(this);
    m_client->setHostname(m_host);
    m_client->setPort(static_cast<quint16>(m_port));
    if (!m_user.isEmpty())
        m_client->setUsername(m_user);
    m_client->setPassword(m_password);
    m_client->setClientId(
        QStringLiteral("smartclip-")
        + QString::number(quintptr(this), 16));
#endif
}

void MqttClient::connectToBroker()
{
#ifdef SMARTCLIP_HAVE_MQTT
    if (!m_client)
        m_client = new QMqttClient(this);
    if (m_client->state() != QMqttClient::Disconnected)
        return;

    connect(m_client, &QMqttClient::connected, this, &MqttClient::onConnected,
            Qt::UniqueConnection);
    connect(m_client, &QMqttClient::disconnected, this,
            &MqttClient::onDisconnected, Qt::UniqueConnection);
    connect(m_client, &QMqttClient::messageReceived, this,
            [this](const QByteArray &message, const QMqttTopicName &topic) {
                emit messageReceived(topic.name(), message);
            });
    connect(m_client, &QMqttClient::errorChanged, this,
            [this](QMqttClient::ClientError e) {
                if (e != QMqttClient::NoError)
                    emit errorOccurred(
                        QStringLiteral("MQTT error: %1").arg(int(e)));
            });

    if (m_tls) {
#ifndef QT_NO_SSL
        m_client->connectToHostEncrypted(QSslConfiguration::defaultConfiguration());
#else
        emit errorOccurred(QStringLiteral("TLS недоступен в этой сборке Qt"));
#endif
    } else {
        m_client->connectToHost();
    }
#endif
}

void MqttClient::disconnectFromBroker()
{
#ifdef SMARTCLIP_HAVE_MQTT
    if (m_client && m_client->state() != QMqttClient::Disconnected)
        m_client->disconnectFromHost();
#endif
}

bool MqttClient::isConnected() const
{
#ifdef SMARTCLIP_HAVE_MQTT
    return m_client && m_client->state() == QMqttClient::Connected;
#else
    return false;
#endif
}

void MqttClient::onConnected()
{
#ifdef SMARTCLIP_HAVE_MQTT
    if (m_client && !m_subscribeTopic.isEmpty())
        m_client->subscribe(QMqttTopicFilter(m_subscribeTopic));
#endif
    emit connected();
}

void MqttClient::onDisconnected()
{
    emit disconnected();
}

void MqttClient::publish(const QString &topic, const QByteArray &payload)
{
#ifdef SMARTCLIP_HAVE_MQTT
    if (!m_client || m_client->state() != QMqttClient::Connected) {
        qWarning() << "MqttClient: publish без соединения";
        return;
    }
    m_client->publish(QMqttTopicName(topic), payload, 0, false);
#else
    Q_UNUSED(topic); Q_UNUSED(payload);
#endif
}

void MqttClient::subscribe(const QString &topic)
{
    m_subscribeTopic = topic;
#ifdef SMARTCLIP_HAVE_MQTT
    if (m_client && m_client->state() == QMqttClient::Connected)
        m_client->subscribe(QMqttTopicFilter(topic));
#endif
}
