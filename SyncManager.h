#pragma once

#include "SyncEngine.h"

#include <QObject>
#include <QByteArray>
#include <QString>

class MqttClient;
class HistoryManager;
class SettingsManager;
class QTimer;

// ─────────────────────────────────────────────────────────────────────────
// SyncManager — оркестратор сетевой синхронизации.
//
//  * Транспорт: MQTT (MqttClient). Топик: smartclip/<room>/state.
//  * Полезная нагрузка шифруется AES-256-GCM; ключ = PBKDF2(пароль, соль=0).
//    На брокере видны только topic + base64-шифртекст — содержимое скрыто.
//  * Роли: master (leading) / slave. Master авторитетен: его избранное/цвета,
//    mask, comment (с оговоркой про пустой comment) — приоритетны.
//  * Реагирует на локальные изменения (publish), на приём (merge+apply),
//    периодически повторяет announce (устойчивость к пропускам).
// ─────────────────────────────────────────────────────────────────────────
class SyncManager final : public QObject
{
    Q_OBJECT

public:
    SyncManager(MqttClient *client, HistoryManager *history,
                SettingsManager *settings, QObject *parent = nullptr);
    ~SyncManager() override = default;

    /** Запустить/перезапустить по текущим настройкам. No-op если синк выключен. */
    void applySettings();
    void stop();
    bool isActive() const;

    /** Строка человекочитаемого статуса для UI (напр. «Подключено»). */
    QString statusText() const;
    /** Человекочитаемая причина, если синк не активен (или пусто). */
    QString inactiveReason() const;

    /** Начать подключение заново (из настроек, applySettings). */
    void reconnectNow();
    /** Разово подключиться и проверить связь (кнопка в настройках). */
    void checkNow();

signals:
    /** Удалённое состояние применено к локальной истории (нужен rebuild UI). */
    void stateApplied();
    /** Статус соединения изменился (для обновления индикатора в UI). */
    void statusChanged();

public slots:
    /** Локальное изменение (буфер/избранное/маска/комментарий/очистка). */
    void notifyLocalChange();

private slots:
    void onConnected();
    void onMessage(const QString &topic, const QByteArray &payload);
    void flushPendingPublish();
    void onClientStatusChanged();

private:
    void publishState();
    QString topic() const;
    QByteArray buildPayload() const;
    SyncEngine::NetworkState localState() const;
    QByteArray encryptionKey() const;

    MqttClient *m_client = nullptr;
    HistoryManager *m_history = nullptr;
    SettingsManager *m_settings = nullptr;

    QString m_deviceId;
    QTimer *m_publishThrottle = nullptr;   // склейка частых publish
    QTimer *m_periodic = nullptr;          // периодический announce
    bool m_pendingPublish = false;
    QByteArray m_lastPublishedHash;        // SHA-256 последнего опубликованного plaintext
                                           // (сравнение эха без хрупкого равенства JSON)
    int m_knownMasterHistorySize = 0;      // транслит History Size мастера (для 2-го хопа)
};
