#include "MqttClient.h"
#include <QDebug>
#include <QTimer>
#include <QRandomGenerator>
#include <functional>

#ifdef SMARTCLIP_HAVE_MQTT
#include <QtMqtt/QMqttClient>
#include <QtMqtt/QMqttTopicName>
#include <QtMqtt/QMqttSubscription>
#include <QtMqtt/QMqttTopicFilter>
#include <QNetworkInformation>
#ifndef QT_NO_SSL
#include <QtNetwork/QSslConfiguration>
#endif

namespace {
// ── Параметры автопереподключения ──────────────────────────────────────
constexpr int kReconnectBaseMs = 2000;   // старт: 2 с
constexpr int kReconnectMaxMs  = 60000;  // потолок: 60 с
constexpr int kJitterPercent   = 20;     // джиттер ±20 % (чтобы клиенты не били синхронно)
}  // namespace

// Регистрируемся на события сетевой доступности ОС (если бэкенд есть).
// Отсутствие бэкенда — не критично: остаётся обычный backoff по таймеру.
static void ensureReachabilityHook(
    bool &hooked, QObject *ctx, const std::function<void()> &onChange)
{
    if (hooked)
        return;
    if (!QNetworkInformation::loadDefaultBackend())
        return;  // бэкенда нет — работаем только по таймеру
    if (auto *ni = QNetworkInformation::instance()) {
        QObject::connect(ni, &QNetworkInformation::reachabilityChanged,
                         ctx, [onChange](QNetworkInformation::Reachability r) {
            if (r == QNetworkInformation::Reachability::Online)
                onChange();
        });
        hooked = true;
    }
}
#endif

MqttClient::MqttClient(QObject *parent)
    : QObject(parent)
{
#ifdef SMARTCLIP_HAVE_MQTT
    m_client = new QMqttClient(this);
    // Сигналы подключаем ОДИН раз здесь (а не в connectToBroker), иначе при
    // каждом applySettings копились бы дублирующие обработчики (двойной приём).
    connect(m_client, &QMqttClient::connected, this, &MqttClient::onConnected);
    connect(m_client, &QMqttClient::disconnected, this, &MqttClient::onDisconnected);
    connect(m_client, &QMqttClient::messageReceived, this,
            [this](const QByteArray &message, const QMqttTopicName &topic) {
                emit messageReceived(topic.name(), message);
            });
    connect(m_client, &QMqttClient::errorChanged, this,
            [this](QMqttClient::ClientError e) {
                if (e == QMqttClient::NoError)
                    return;
                const QString err = QStringLiteral("MQTT error: %1").arg(int(e));
                setStatus(Status::Error, err);
                emit errorOccurred(err);
                // Автопереподключение: ошибка соединения → планируем ретрай.
                scheduleReconnect();
            });
    connect(m_client, &QMqttClient::stateChanged, this,
            [this](QMqttClient::ClientState st) {
                if (st == QMqttClient::Connecting)
                    setStatus(Status::Connecting);
                else if (st == QMqttClient::Disconnected
                         && m_status != Status::Error)
                    setStatus(Status::Disconnected);
            });

    // Backoff-таймер автопереподключения (single-shot, интервал задаём на лету).
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &MqttClient::onReconnectTimeout);

    // Мгновенный ретрай при возврате сети (если ОС отдаёт события).
    ensureReachabilityHook(m_reachabilityHooked, this, [this]() { tryNow(); });
#else
    m_status = Status::Unavailable;
#endif
}

MqttClient::~MqttClient() = default;

void MqttClient::setStatus(Status s, const QString &error)
{
    if (m_status == s && m_lastError == error)
        return;
    m_status = s;
    m_lastError = error;
    emit statusChanged();
}

QString MqttClient::statusText() const
{
    if (!available())
        return QStringLiteral("Недоступно: сборка без модуля Qt6::Mqtt");
    switch (m_status) {
    case Status::Unavailable:  return QStringLiteral("Недоступно");
    case Status::Disconnected: return QStringLiteral("Отключено");
    case Status::Connecting:   return QStringLiteral("Подключение…");
    case Status::Connected:    return QStringLiteral("Подключено");
    case Status::Error:        return QStringLiteral("Ошибка: ") + m_lastError;
    }
    return QString();
}

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
    m_wanted = true;
    if (m_client && m_client->state() != QMqttClient::Disconnected)
        return;  // уже подключаемся/подключены — попытки не плодят
    startConnect();
#endif
}

#ifdef SMARTCLIP_HAVE_MQTT
void MqttClient::startConnect()
{
    if (!m_client)
        m_client = new QMqttClient(this);
    if (m_client->state() != QMqttClient::Disconnected)
        return;

    m_lastError.clear();
    setStatus(Status::Connecting);

    if (m_tls) {
#ifndef QT_NO_SSL
        m_client->connectToHostEncrypted(QSslConfiguration::defaultConfiguration());
#else
        setStatus(Status::Error, QStringLiteral("TLS недоступен в этой сборке Qt"));
        emit errorOccurred(QStringLiteral("TLS недоступен в этой сборке Qt"));
#endif
    } else {
        m_client->connectToHost();
    }
}

void MqttClient::scheduleReconnect()
{
    // Только если синк действительно хочет быть подключён и нет активной попытки.
    if (!m_wanted || !m_reconnectTimer)
        return;
    if (m_client && m_client->state() != QMqttClient::Disconnected)
        return;
    if (m_reconnectTimer->isActive())
        return;

    // Экспоненциальный рост: 2с → 4 → 8 … → 60с (потолок).
    if (m_reconnectDelayMs <= 0)
        m_reconnectDelayMs = kReconnectBaseMs;
    else
        m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, kReconnectMaxMs);

    // Джиттер ±20 %: сдвигаем интервал случайно, чтобы два клиента
    // (ноут + iMac) не били в брокер строго синхронно.
    const int jitterRange = m_reconnectDelayMs * kJitterPercent / 100;
    const int jitter = jitterRange > 0
        ? QRandomGenerator::global()->bounded(-jitterRange, jitterRange + 1)
        : 0;
    const int delay = qMax(500, m_reconnectDelayMs + jitter);

    m_reconnectTimer->start(delay);
}

void MqttClient::onReconnectTimeout()
{
    if (!m_wanted)
        return;
    startConnect();
    // Если попытка снова провалится, errorChanged/stateChanged снова вызовут
    // scheduleReconnect() и интервал удвоится. При успехе backoff сброшен
    // в onConnected().
}

void MqttClient::tryNow()
{
    // Событие «сеть вернулась»: не ждём текущий backoff — пробуем сразу.
    if (!m_wanted)
        return;
    if (m_client && m_client->state() != QMqttClient::Disconnected)
        return;
    m_reconnectDelayMs = 0;  // следующий backoff — снова со стартовых 2 с
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    startConnect();
}
#endif

void MqttClient::disconnectFromBroker()
{
#ifdef SMARTCLIP_HAVE_MQTT
    m_wanted = false;
    m_reconnectDelayMs = 0;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
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
    // Успешный коннект → сбрасываем backoff к старту.
    m_reconnectDelayMs = 0;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (m_client && !m_subscribeTopic.isEmpty())
        m_client->subscribe(QMqttTopicFilter(m_subscribeTopic));
#endif
    setStatus(Status::Connected);
    emit connected();
}

void MqttClient::onDisconnected()
{
    if (m_status != Status::Error)
        setStatus(Status::Disconnected);
#ifdef SMARTCLIP_HAVE_MQTT
    // Обрыв (брокер/сеть) при взведённом m_wanted → планируем возврат.
    scheduleReconnect();
#endif
}

void MqttClient::publish(const QString &topic, const QByteArray &payload,
                         bool retain)
{
#ifdef SMARTCLIP_HAVE_MQTT
    if (!m_client || m_client->state() != QMqttClient::Connected) {
        qWarning() << "MqttClient: publish без соединения";
        return;
    }
    // QoS 1 (at-least-once) + optionally retain: клип не теряется при обрыве,
    // а retained-состояние отдаётся новым подписчикам сразу при connect.
    m_client->publish(QMqttTopicName(topic), payload, 1, retain);
#else
    Q_UNUSED(topic); Q_UNUSED(payload); Q_UNUSED(retain);
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
