#include "Crypto.h"

#include <QProcess>
#include <QByteArray>
#include <QDebug>
#include <QThread>
#include <cstring>

#if defined(SMARTCLIP_HAVE_OPENSSL)
 #include <openssl/evp.h>
 #include <openssl/rand.h>
#endif

namespace {

constexpr int kKeyLen = 32;    // AES-256
constexpr int kNonceLen = 12;  // GCM
constexpr int kTagLen = 16;    // GCM

QByteArray runCapture(const QString &prog, const QStringList &args,
                      bool *ok = nullptr)
{
    QProcess p;
    p.start(prog, args);
    if (!p.waitForFinished(3000)) {
        if (ok) *ok = false;
        return {};
    }
    if (ok) *ok = (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0);
    return p.readAllStandardOutput();
}

}  // namespace

namespace Crypto {

bool available()
{
#if defined(SMARTCLIP_HAVE_OPENSSL)
    return true;
#else
    return false;
#endif
}

QByteArray randomBytes(int n)
{
    QByteArray out(n, 0);
    if (n <= 0)
        return out;
#if defined(SMARTCLIP_HAVE_OPENSSL)
    // Единственный источник: CSPRNG OpenSSL. Фолбэка НЕТ намеренно: иначе
    // при сбое энтропии можно было бы сгенерировать предсказуемый ключ и
    // сломать шифрование, оставаясь в неведении. Возврат пустого → вызов
    // обрабатывает ошибку как «шифрование недоступно».
    if (RAND_bytes(reinterpret_cast<unsigned char *>(out.data()), n) != 1)
        return {};
    return out;
#else
    return {};
#endif
}

bool init()
{
#if defined(SMARTCLIP_HAVE_OPENSSL)
    // Просто проверяем, что EVP доступен (внешняя зависимость OpenSSL).
    return EVP_get_digestbyname("sha256") != nullptr;
#else
    return false;
#endif
}

QByteArray deriveKey(const QString &password, const QByteArray &salt,
                     int iterations)
{
#if !defined(SMARTCLIP_HAVE_OPENSSL)
    Q_UNUSED(password); Q_UNUSED(salt); Q_UNUSED(iterations);
    return {};
#else
    if (password.isEmpty() || iterations <= 0)
        return {};
    const QByteArray pw = password.toUtf8();
    const QByteArray sl = salt.isEmpty() ? QByteArray(1, '\0') : salt;
    QByteArray out(kKeyLen, 0);
    if (PKCS5_PBKDF2_HMAC(pw.constData(), pw.size(),
                          reinterpret_cast<const unsigned char *>(sl.constData()),
                          sl.size(), iterations, EVP_sha256(), kKeyLen,
                          reinterpret_cast<unsigned char *>(out.data())) != 1)
        return {};
    return out;
#endif
}

QByteArray encrypt(const QByteArray &plain, const QByteArray &key)
{
#if !defined(SMARTCLIP_HAVE_OPENSSL)
    Q_UNUSED(plain); Q_UNUSED(key);
    return {};
#else
    if (key.size() != kKeyLen)
        return {};
    QByteArray nonce = randomBytes(kNonceLen);
    QByteArray out(kNonceLen + plain.size() + kTagLen, 0);
    std::memcpy(out.data(), nonce.constData(), kNonceLen);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};
    int len = 0, total = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr,
                                 nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen,
                               nullptr) == 1
        && EVP_EncryptInit_ex(
               ctx, nullptr, nullptr,
               reinterpret_cast<const unsigned char *>(key.constData()),
               reinterpret_cast<const unsigned char *>(nonce.constData())) == 1
        && EVP_EncryptUpdate(
               ctx, reinterpret_cast<unsigned char *>(out.data() + kNonceLen),
               &len, reinterpret_cast<const unsigned char *>(plain.constData()),
               plain.size()) == 1;
    total = len;
    if (ok) {
        ok = EVP_EncryptFinal_ex(
                 ctx,
                 reinterpret_cast<unsigned char *>(out.data() + kNonceLen
                                                  + total),
                 &len) == 1;
        total += len;
    }
    if (ok) {
        unsigned char tag[kTagLen];
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag) == 1;
        if (ok)
            std::memcpy(out.data() + kNonceLen + total, tag, kTagLen);
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};
    return out.left(kNonceLen + total + kTagLen);
#endif
}

QByteArray decrypt(const QByteArray &blob, const QByteArray &key, bool *ok)
{
    if (ok)
        *ok = false;
#if !defined(SMARTCLIP_HAVE_OPENSSL)
    Q_UNUSED(blob); Q_UNUSED(key);
    return {};
#else
    if (key.size() != kKeyLen || blob.size() < kNonceLen + kTagLen)
        return {};
    const int ctLen = blob.size() - kNonceLen - kTagLen;
    QByteArray out(ctLen, 0);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};
    int len = 0, total = 0;
    bool good = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr,
                                   nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen,
                               nullptr) == 1
        && EVP_DecryptInit_ex(
               ctx, nullptr, nullptr,
               reinterpret_cast<const unsigned char *>(key.constData()),
               reinterpret_cast<const unsigned char *>(blob.constData())) == 1
        && EVP_DecryptUpdate(
               ctx, reinterpret_cast<unsigned char *>(out.data()), &len,
               reinterpret_cast<const unsigned char *>(blob.constData()
                                                       + kNonceLen),
               ctLen) == 1;
    total = len;
    if (good) {
        good = EVP_CIPHER_CTX_ctrl(
                   ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                   const_cast<char *>(blob.constData() + kNonceLen + ctLen)) == 1;
    }
    int fin = 0;
    if (good)
        good = EVP_DecryptFinal_ex(
                   ctx, reinterpret_cast<unsigned char *>(out.data() + total),
                   &fin) == 1;
    total += fin;
    EVP_CIPHER_CTX_free(ctx);
    if (!good)
        return {};
    out.truncate(total);
    if (ok)
        *ok = true;
    return out;
#endif
}

// ─────────────────────────── хранилище ключа ───────────────────────────

QString keyringBackend()
{
#if defined(Q_OS_MAC)
    return QStringLiteral("macOS Keychain");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("Secret Service");
#else
    return QString();
#endif
}

QByteArray loadKey(const QString &service, const QString &account)
{
#if defined(Q_OS_MAC)
    bool ok = false;
    const QByteArray out = runCapture(
        "security", {"find-generic-password", "-s", service, "-a", account,
                     "-w"}, &ok);
    if (!ok)
        return {};
    return QByteArray::fromBase64(out.trimmed());
#elif defined(Q_OS_LINUX)
    bool ok = false;
    const QByteArray out = runCapture(
        "secret-tool", {"lookup", "service", service, "account", account}, &ok);
    if (!ok)
        return {};
    return QByteArray::fromBase64(out.trimmed());
#else
    Q_UNUSED(service); Q_UNUSED(account);
    return {};
#endif
}

bool storeKey(const QString &service, const QString &account,
              const QByteArray &key)
{
    if (key.size() != kKeyLen)
        return false;   // не сохраняем пустой/битый ключ
    const QByteArray b64 = key.toBase64();
#if defined(Q_OS_MAC)
    const QString pw = QString::fromLatin1(b64);
    bool ok = false;
    runCapture("security", {"add-generic-password", "-s", service, "-a", account,
                            "-w", pw, "-U"}, &ok);
    return ok;
#elif defined(Q_OS_LINUX)
    QProcess p;
    p.start("secret-tool",
            {"store", "--label", service, "service", service,
             "account", account});
    if (!p.waitForStarted(3000))
        return false;
    p.write(b64);
    p.write("\n");
    p.closeWriteChannel();
    if (!p.waitForFinished(3000))
        return false;
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
#else
    Q_UNUSED(service); Q_UNUSED(account); Q_UNUSED(key);
    return false;
#endif
}

QByteArray loadOrCreateKey(const QString &service, const QString &account)
{
    // Ретраи: демон keyring (gnome-keyring/KWallet/Secret Service) при старте
    // сессии может подниматься ПОЗЖЕ приложения (гонка на автозапуске). Две
    // короткие попытки с паузой закрывают подавляющую часть таких случаев.
    constexpr int kRetries = 2;          // дополнительных попыток после первой
    constexpr int kRetryDelayMs = 700;

    QByteArray key;
    for (int attempt = 0; attempt <= kRetries; ++attempt) {
        if (!init())
            break;                        // нет OpenSSL — ретраи бессмысленны
        key = loadKey(service, account);
        if (key.size() == kKeyLen)
            return key;
        if (attempt < kRetries)
            QThread::msleep(kRetryDelayMs);
    }

    // Ключа нет — создаём новый (только если CSPRNG работает).
    key = randomBytes(kKeyLen);
    if (key.size() != kKeyLen) {
        qWarning() << "SmartClip: key generator unavailable (no CSPRNG)";
        return {};
    }

    // Запись в keyring — тоже с ретраями (демон мог ещё не быть готов).
    for (int attempt = 0; attempt <= kRetries; ++attempt) {
        if (storeKey(service, account, key))
            return key;
        if (attempt < kRetries)
            QThread::msleep(kRetryDelayMs);
    }

    qWarning() << "SmartClip: failed to store the encryption key in"
               << keyringBackend();
    return {};
}

}  // namespace Crypto
