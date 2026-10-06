#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>

#ifdef SMARTCLIP_HAVE_MQTT
class QMqttClient;
#endif
class QTimer;

// ─────────────────────────────────────────────────────────────────────────
// MqttClient — тонкая обёртка над QMqttClient(Qt6::Mqtt).
// Если модуль MQTT не собран (SMARTCLIP_HAVE_MQTT не определён), класс
// существует, но методы — no-op: приложение собирается и работает без синка.
// ─────────────────────────────────────────────────────────────────────────
class MqttClient final : public QObject
{
    Q_OBJECT

public:
    /** Состояние соединения с брокером (для отображения в UI). */
    enum class Status {
        Unavailable,   // модуль Qt MQTT не собран
        Disconnected,  // настроен, но не подключён
        Connecting,
        Connected,
        Error,         // ошибка соединения (текст — в statusText())
    };

    explicit MqttClient(QObject *parent = nullptr);
    ~MqttClient() override;

    void configure(const QString &host, int port, bool tls,
                   const QString &user, const QString &password);
    /** Подключиться. Если уже подключены — ничего не делает. */
    void connectToBroker();
    void disconnectFromBroker();
    bool isConnected() const;

    /** Публикация сырого payload (шифрование — на стороне SyncManager).
     *  QoS 1 (at-least-once). retain=true — брокер хранит последнее состояние
     *  и отдаёт его новым подписчикам сразу при connect (догон гарантирован). */
    void publish(const QString &topic, const QByteArray &payload,
                 bool retain = false);
    /** Подписка на топик. Переподписка происходит автоматически при reconnect. */
    void subscribe(const QString &topic);

    static bool available();

    QString statusText() const;

signals:
    void connected();
    void messageReceived(const QString &topic, const QByteArray &payload);
    void errorOccurred(const QString &error);
    /** Статус изменился (для обновления индикатора в UI). */
    void statusChanged();

private slots:
    void onConnected();
    void onDisconnected();

private:
    void setStatus(Status s, const QString &error = QString());

#ifdef SMARTCLIP_HAVE_MQTT
    /** Планирует следующую попытку переподключения (только если m_wanted).
     *  Экспоненциальный backoff: старт 2 с, ×2, потолок 60 с, джиттер ±20 %.
     *  Повторные вызовы, пока таймер уже идёт, игнорируются. */
    void scheduleReconnect();
    /** Фактический запуск соединения (без правки флага/backoff). */
    void startConnect();
    /** Немедленная попытка (событие «сеть вернулась»): гасит текущий таймер,
     *  сокращает backoff к старту и пробует подключиться прямо сейчас. */
    void tryNow();
    void onReconnectTimeout();
#endif

    QString m_host;
    int m_port = 1883;
    bool m_tls = false;
    QString m_user;
    QString m_password;
    QString m_subscribeTopic;   // на что подписаны (переподписываемся при connect)
    Status m_status = Status::Disconnected;
    QString m_lastError;

    // Флаг «хочу быть подключён»: взводится connectToBroker(), снимается
    // disconnectFromBroker(). Пока взведён — при обрыве/ошибке планируется
    // автопереподключение. Осознанный stop() попыток не крутит.
    bool m_wanted = false;

#ifdef SMARTCLIP_HAVE_MQTT
    QMqttClient *m_client = nullptr;
    QTimer *m_reconnectTimer = nullptr;   // backoff-таймер автопереподключения
    int m_reconnectDelayMs = 0;           // текущая задержка (0 = планируем впервые)
    bool m_reachabilityHooked = false;    // подписались ли на события сети
#endif
};
