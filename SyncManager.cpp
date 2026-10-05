#include "SyncManager.h"
#include "MqttClient.h"
#include "HistoryManager.h"
#include "SettingsManager.h"
#include "Crypto.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QDebug>

namespace {

constexpr int kPublishThrottleMs = 700;
constexpr int kPeriodicAnnounceMs = 60000;

QByteArray stateToJson(const SyncEngine::NetworkState &st,
                       const QString &deviceId = QString(),
                       int historySize = 0,
                       const QString &role = QString(),
                       int masterHistorySize = 0)
{
    QJsonArray items;
    for (const auto &it : st.items) {
        QJsonObject o;
        o.insert(QStringLiteral("t"), it.text);
        o.insert(QStringLiteral("u"), it.usageCount);
        o.insert(QStringLiteral("a"), double(it.addedAtMs));
        o.insert(QStringLiteral("c"), it.favoriteColorIndex);
        o.insert(QStringLiteral("m"), it.maskInMenu ? 1 : 0);
        if (!it.comment.isEmpty())
            o.insert(QStringLiteral("p"), it.comment);
        // Метки последней правки полей (LWW). 0 = не менялось → поле не шлём.
        if (it.favChangedAtMs > 0)
            o.insert(QStringLiteral("fc"), double(it.favChangedAtMs));
        if (it.maskChangedAtMs > 0)
            o.insert(QStringLiteral("mc"), double(it.maskChangedAtMs));
        if (it.commentChangedAtMs > 0)
            o.insert(QStringLiteral("pc"), double(it.commentChangedAtMs));
        items.append(o);
    }
    QJsonArray del;
    for (auto it = st.tombstones.constBegin(); it != st.tombstones.constEnd(); ++it) {
        if (it.key().isEmpty())
            continue;
        QJsonArray pair;
        pair.append(it.key());
        pair.append(double(it.value()));
        del.append(pair);
    }
    QJsonObject root;
    root.insert(QStringLiteral("v"), 1);
    if (!deviceId.isEmpty())
        root.insert(QStringLiteral("d"), deviceId);
    if (historySize > 0)
        root.insert(QStringLiteral("hs"), historySize);
    if (!role.isEmpty())
        root.insert(QStringLiteral("role"), role);
    if (masterHistorySize > 0)
        root.insert(QStringLiteral("mhs"), masterHistorySize);
    root.insert(QStringLiteral("items"), items);
    root.insert(QStringLiteral("deleted"), del);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString jsonDeviceId(const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return QString();
    return doc.object().value(QStringLiteral("d")).toString();
}

SyncEngine::NetworkState jsonToState(const QByteArray &data)
{
    SyncEngine::NetworkState st;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return st;
    const QJsonObject root = doc.object();

    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        HistoryManager::HistoryItem it;
        it.text = o.value(QStringLiteral("t")).toString();
        if (it.text.isEmpty())
            continue;
        it.usageCount = o.value(QStringLiteral("u")).toInt();
        it.addedAtMs = qint64(o.value(QStringLiteral("a")).toDouble());
        int c = o.value(QStringLiteral("c")).toInt(-1);
        if (c < -1 || c > 32)
            c = -1;
        it.favoriteColorIndex = c;
        it.maskInMenu = o.value(QStringLiteral("m")).toInt() != 0;
        it.comment = o.value(QStringLiteral("p")).toString();
        it.favChangedAtMs = qint64(o.value(QStringLiteral("fc")).toDouble(0));
        it.maskChangedAtMs = qint64(o.value(QStringLiteral("mc")).toDouble(0));
        it.commentChangedAtMs = qint64(o.value(QStringLiteral("pc")).toDouble(0));
        st.items.push_back(it);
    }

    const QJsonArray del = root.value(QStringLiteral("deleted")).toArray();
    for (const QJsonValue &v : del) {
        const QJsonArray pair = v.toArray();
        if (pair.size() < 2)
            continue;
        const QString t = pair.at(0).toString();
        const qint64 at = qint64(pair.at(1).toDouble());
        if (!t.isEmpty() && at > 0)
            st.tombstones.insert(t, at);
    }
    return st;
}

}  // namespace

SyncManager::SyncManager(MqttClient *client, HistoryManager *history,
                         SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_history(history)
    , m_settings(settings)
{
    m_deviceId = QStringLiteral("sc-")
        + QString::number(QRandomGenerator::global()->generate(), 16);

    m_publishThrottle = new QTimer(this);
    m_publishThrottle->setSingleShot(true);
    m_publishThrottle->setInterval(kPublishThrottleMs);
    connect(m_publishThrottle, &QTimer::timeout,
            this, &SyncManager::flushPendingPublish);

    m_periodic = new QTimer(this);
    m_periodic->setInterval(kPeriodicAnnounceMs);
    connect(m_periodic, &QTimer::timeout, this, [this]() {
        // Периодический announce: помогает «догонять» пропущенные сообщения.
        publishState();
    });

    if (m_client) {
        connect(m_client, &MqttClient::connected,
                this, &SyncManager::onConnected);
        connect(m_client, &MqttClient::messageReceived,
                this, &SyncManager::onMessage);
        connect(m_client, &MqttClient::errorOccurred, this,
                [](const QString &e) { qWarning() << "SmartClip sync:" << e; });
        // Пробрасываем изменение статуса в UI (индикатор в настройках).
        connect(m_client, &MqttClient::statusChanged,
                this, &SyncManager::onClientStatusChanged);
    }
}

void SyncManager::onClientStatusChanged()
{
    emit statusChanged();
}

bool SyncManager::isActive() const
{
    return m_periodic && m_periodic->isActive();
}

QString SyncManager::inactiveReason() const
{
    if (isActive())
        return QString();
    if (!m_settings || !m_settings->syncEnabled())
        return QStringLiteral("синхронизация выключена");
    if (!MqttClient::available())
        return QStringLiteral("сборка без модуля Qt6::Mqtt");
    if (m_settings->brokerHost().trimmed().isEmpty())
        return QStringLiteral("не задан Broker host");
    if (m_settings->syncEncryptionPassword().isEmpty())
        return QStringLiteral("не задан Encryption password");
    return QStringLiteral("не активна");
}

QString SyncManager::statusText() const
{
    if (!m_client)
        return QStringLiteral("Нет клиента");
    if (!isActive()) {
        const QString why = inactiveReason();
        return why.isEmpty() ? QStringLiteral("Отключено")
                             : QStringLiteral("Выключено (") + why + QStringLiteral(")");
    }
    return m_client->statusText();
}

void SyncManager::reconnectNow()
{
    if (!isActive())
        return;
    if (m_client)
        m_client->connectToBroker();
}

void SyncManager::checkNow()
{
    // Кнопка «Проверить»: применяем текущие настройки и пробуем подключиться,
    // не дожидаясь OK. Настройки уже сохранены менеджером при изменении полей.
    applySettings();
    reconnectNow();
}

QString SyncManager::topic() const
{
    QString room = m_settings ? m_settings->syncRoom() : QStringLiteral("smartclip");
    if (room.isEmpty())
        room = QStringLiteral("smartclip");
    room.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return QStringLiteral("smartclip/") + room + QStringLiteral("/state");
}

QByteArray SyncManager::encryptionKey() const
{
    if (!m_settings)
        return {};
    const QString pw = m_settings->syncEncryptionPassword();
    if (pw.isEmpty())
        return {};
    // Соль — константа приложения (по договорённости: salt = 0).
    return Crypto::deriveKey(pw, QByteArray(1, '\0'));
}

void SyncManager::applySettings()
{
    if (!m_client || !m_settings || !m_history)
        return;

    const bool want = m_settings->syncEnabled()
                      && MqttClient::available()
                      && !m_settings->brokerHost().trimmed().isEmpty()
                      && !m_settings->syncEncryptionPassword().isEmpty();

    if (!want) {
        stop();
        emit statusChanged();
        return;
    }

    m_client->configure(m_settings->brokerHost(),
                        m_settings->brokerPort(),
                        m_settings->useTls(),
                        m_settings->brokerUser(),
                        m_settings->brokerPassword());
    m_client->subscribe(topic());
    m_client->connectToBroker();

    if (!m_periodic->isActive())
        m_periodic->start();

    emit statusChanged();
}

void SyncManager::stop()
{
    if (m_periodic)
        m_periodic->stop();
    if (m_publishThrottle)
        m_publishThrottle->stop();
    m_pendingPublish = false;
    m_lastPublishedHash.clear();
    if (m_client)
        m_client->disconnectFromBroker();
}

void SyncManager::onConnected()
{
    // Сразу объявляем своё состояние, чтобы синхронизировать догоняющего.
    publishState();
}

SyncEngine::NetworkState SyncManager::localState() const
{
    SyncEngine::NetworkState st;
    if (!m_history)
        return st;
    st.items = m_history->history();
    st.tombstones = m_history->tombstones();
    return st;
}

QByteArray SyncManager::buildPayload() const
{
    // В пакет кладём deviceId (чтобы игнорировать собственное эхо), роль и
    // History Size, чтобы ведущий мог авторитетно назначить лимит истории.
    const QString role = (m_settings && !m_settings->isMaster())
                             ? QStringLiteral("slave")
                             : QStringLiteral("master");
    // mhs: «известный History Size мастера» — транслируем от мастера через
    // ведомых, чтобы и второй хоп (следующий ведомый) его получил.
    const int mhs = (m_settings && m_settings->isMaster())
                        ? m_settings->maxItems()
                        : m_knownMasterHistorySize;
    return stateToJson(localState(), m_deviceId,
                       m_settings ? m_settings->maxItems() : 0, role, mhs);
}

void SyncManager::notifyLocalChange()
{
    if (!isActive() || !m_client || !m_client->isConnected())
        return;
    m_pendingPublish = true;
    if (m_publishThrottle && !m_publishThrottle->isActive())
        m_publishThrottle->start();
}

void SyncManager::flushPendingPublish()
{
    if (!m_pendingPublish)
        return;
    m_pendingPublish = false;
    publishState();
}

void SyncManager::publishState()
{
    if (!m_client || !m_client->isConnected())
        return;
    const QByteArray key = encryptionKey();
    if (key.size() != 32)
        return;

    const QByteArray raw = buildPayload();        // с id устройства — уходит в сеть
    const QByteArray canonical = stateToJson(localState());  // без id — для сравнений

    QByteArray blob = Crypto::encrypt(raw, key);
    if (blob.isEmpty())
        return;

    m_client->publish(topic(), blob.toBase64(), true);
    m_lastPublishedHash = QCryptographicHash::hash(canonical,
                                                   QCryptographicHash::Sha256);
}

void SyncManager::onMessage(const QString &topicName, const QByteArray &payload)
{
    Q_UNUSED(topicName);
    if (!m_client || !m_history || !m_settings)
        return;

    const QByteArray key = encryptionKey();
    if (key.size() != 32)
        return;

    const QByteArray blob = QByteArray::fromBase64(payload);
    bool ok = false;
    const QByteArray plain = Crypto::decrypt(blob, key, &ok);
    if (!ok) {
        qWarning() << "SmartClip sync: не расшифровал входящий пакет "
                      "(неверный общий пароль?)";
        return;
    }

    // Игнорируем собственное эхо (брокер присылает нам нашу же публикацию —
    // иначе устаревшая копия могла бы «перевернуть» цвета избранного).
    if (jsonDeviceId(plain) == m_deviceId)
        return;

    const SyncEngine::NetworkState remote = jsonToState(plain);

    // History Size назначает ведущий: если отправитель — master, принимаем его
    // лимит как свой (требование: «значение ведущего назначается всем ведомым»).
    const QJsonObject root = QJsonDocument::fromJson(plain).object();
    const QString remoteRole = root.value(QStringLiteral("role")).toString();
    const int remoteHs = root.value(QStringLiteral("hs")).toInt(0);
    const int remoteMhs = root.value(QStringLiteral("mhs")).toInt(0);

    // Обновляем знание о «History Size мастера»: либо от самого мастера (hs),
    // либо транслитом от другого ведомого (mhs).
    const int knownMasterHs = (remoteRole == QLatin1String("master"))
                                  ? remoteHs
                                  : remoteMhs;
    if (knownMasterHs > 0)
        m_knownMasterHistorySize = knownMasterHs;

    // History Size назначает ведущий: ведомый принимает лимит мастера как свой.
    if (m_settings && !m_settings->isMaster() && knownMasterHs > 0
        && knownMasterHs != m_settings->maxItems()) {
        m_settings->setMaxItems(knownMasterHs);
        m_history->setMaxItems(knownMasterHs);
    }

    const SyncEngine::NetworkState wasLocal = localState();
    const QByteArray wasLocalRaw = stateToJson(wasLocal);

    const SyncEngine::NetworkState merged =
        SyncEngine::merge(wasLocal, remote, m_settings->isMaster(),
                          m_settings->maxItems());
    const QByteArray mergedRaw = stateToJson(merged);

    // Применяем, только если что-то реально изменилось.
    if (mergedRaw != wasLocalRaw) {
        m_history->setState(merged.items, merged.tombstones);
        emit stateApplied();
    }

    // Разошлём объединённое состояние дальше, только если мы добавили что-то
    // новое относительно входящего пакета (иначе — лишний трафик).
    // Сравниваем ХЕШИ: точное сравнение JSON-строк хрупко (порядок ключей,
    // формат чисел/float со временем может отличаться → ложные «изменения»
    // и лишние публикации).
    const QByteArray mergedHash = QCryptographicHash::hash(mergedRaw,
                                                           QCryptographicHash::Sha256);
    const QByteArray remoteHash = QCryptographicHash::hash(stateToJson(remote),
                                                           QCryptographicHash::Sha256);
    if (mergedHash != remoteHash && mergedHash != m_lastPublishedHash) {
        // ВАЖНО: ретрансляция должна сохранять СВОИ deviceId/role/hs, иначе
        // метаданные теряются на втором хопе и History Size мастера не доедет.
        const QString myRole = !m_settings->isMaster()
                                   ? QStringLiteral("slave")
                                   : QStringLiteral("master");
        const QByteArray outRaw = stateToJson(merged, m_deviceId,
                                              m_settings->maxItems(), myRole,
                                              m_knownMasterHistorySize);
        const QByteArray blobOut = Crypto::encrypt(outRaw, key);
        if (!blobOut.isEmpty()) {
            m_client->publish(topic(), blobOut.toBase64(), true);
            m_lastPublishedHash = mergedHash;
        }
    }
}
