#pragma once

#include <QByteArray>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────
// Кроссплатформенное шифрование истории (macOS + Linux).
//
// Схема: AES-256-GCM (OpenSSL EVP). Один и тот же код на обеих платформах;
// различается только способ ХРАНЕНИЯ КЛЮЧА:
//   * macOS  — Keychain           (утилита `security`)
//   * Linux  — Secret Service     (libsecret, утилита `secret-tool`,
//                                  gnome-keyring / KWallet)
// Ключ — 32 случайных байта (base64 в хранилище). Формат шифртекста:
//     nonce(12) || ciphertext || tag(16)   (в base64 на диске)
// ─────────────────────────────────────────────────────────────────────────
namespace Crypto {

/** Доступен ли AES-GCM (собран ли с OpenSSL). */
bool available();

/**
 * Инициализация/проверка доступности крипто-бэкенда (OpenSSL).
 * ВНЕШНЯЯ зависимость: если false — шифрование невозможно, и приложение
 * обязано работать в fail-closed режиме (не писать историю открытым текстом).
 */
bool init();

/** n случайных байт (для ключей/nonce). */
QByteArray randomBytes(int n);

/** AES-256-GCM: ключ ровно 32 байта. Возврат: nonce||ct||tag (пусто — ошибка). */
QByteArray encrypt(const QByteArray &plain, const QByteArray &key);

/**
 * Вывести 32-байтовый ключ из пароля (PBKDF2-HMAC-SHA256).
 * Пароль общий для всех устройств группы; соль — константа приложения
 * (по договорённости с Лёшей: salt = 0). Пусто — OpenSSL недоступен.
 */
QByteArray deriveKey(const QString &password, const QByteArray &salt,
                     int iterations = 120000);

/** Расшифровать blob (nonce||ct||tag). ok=false при неверном ключе/повреждении. */
QByteArray decrypt(const QByteArray &blob, const QByteArray &key, bool *ok = nullptr);

// --- хранилище ключа (Keychain / Secret Service) ---
/** Найти ключ в системном хранилище; пусто, если нет/недоступно. */
QByteArray loadKey(const QString &service, const QString &account);
/** Положить ключ в системное хранилище. false — не удалось. */
bool storeKey(const QString &service, const QString &account, const QByteArray &key);
/** Загрузить ключ, а если его нет — создать и сохранить. Пусто — хранилище недоступно. */
QByteArray loadOrCreateKey(const QString &service, const QString &account);

/** Имя бэкенда хранилища для диагностики ("macOS Keychain", "Secret Service", ""). */
QString keyringBackend();

}  // namespace Crypto
