#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <QHash>

class HistoryManager final : public QObject
{
    Q_OBJECT

public:
    struct HistoryItem {
        QString text;
        int usageCount = 0;
        qint64 addedAtMs = 0;
        /** Index of favorite color (0..32), or -1 if not favorite. */
        int favoriteColorIndex = -1;
        /** If true, show masked text in tray menu (e.g. pas***ord). */
        bool maskInMenu = false;
        /** Пользовательский комментарий к записи (показывается в скобках). */
        QString comment;

        // ── Метки последнего ИЗМЕНЕНИЯ поля (мс, стенное время). ──────────
        // 0 = поле пользователь НИКОГДА не менял → при синке действует
        // старое правило «приоритет ведущего». >0 = время правки → при
        // слиянии полей побеждает версия с большей меткой (LWW).
        qint64 favChangedAtMs = 0;
        qint64 maskChangedAtMs = 0;
        qint64 commentChangedAtMs = 0;
    };

    explicit HistoryManager(QObject *parent = nullptr);
    ~HistoryManager() = default;

    const QVector<HistoryItem> &history() const;
    int maxItems() const;
    void setMaxItems(int maxItems);
    bool isDirty() const;
    void clearDirty();

    void addToHistory(const QString &text);
    void trimToMaxItems();

    /** Ключ шифрования (AES-256-GCM). Пусто — шифрование недоступно. */
    void setEncryptionKey(const QByteArray &key) { m_key = key; }
    bool encryptionEnabled() const { return m_key.size() == 32; }

    /**
     * Загрузить историю с диска.
     * Шифртекст (строки "v2|<base64>") расшифровывается ключом.
     * Старый ОТКРЫТЫЙ формат → парсится, помечается dirty и перезаписывается
     * в зашифрованном виде (миграция) — если ключ задан.
     */
    bool loadHistory(const QString &filePath);
    /** Загрузить и применить лимит из настроек (обёртка). */
    void loadHistory(const QString &filePath, int maxItemsForTrim);
    /** Сохранить историю (шифрованно, если есть ключ; иначе — открытый YAML). */
    void saveHistory(const QString &filePath) const;
    
    // Methods for favorites
    void toggleFavorite(const QString &text);
    bool isFavorite(const QString &text) const;
    void setFavoriteColor(const QString &text, int colorIndex,
                          qint64 changedAtMs = 0);
    int favoriteColorIndex(const QString &text) const;
    void sortHistory();
    
    // Method for usage count
    void incrementUsageCount(const QString &text);

    // Mask in menu (encrypted display)
    void setMaskInMenu(const QString &text, bool mask, qint64 changedAtMs = 0);
    bool maskInMenu(const QString &text) const;

    // Комментарий к записи (режим «Комментарии»)
    void setComment(const QString &text, const QString &comment,
                    qint64 changedAtMs = 0);
    QString comment(const QString &text) const;

    // Method to clear history
    void clearHistory();

    // ─────────────────────────── синхронизация ───────────────────────────
    // Tombstone'ы: текст → время удаления (мс). Хранят факт явного удаления,
    // чтобы удаление не «воскресало» при синке. Если запись с тем же текстом
    // будет скопирована снова, tombstone снимается (addToHistory).
    const QHash<QString, qint64> &tombstones() const { return m_tombstones; }
    bool isDeleted(const QString &text) const { return m_tombstones.contains(text); }
    qint64 tombstoneAtMs(const QString &text) const { return m_tombstones.value(text, 0); }
    /** Пометить запись удалённой и убрать её из списка (tombstone создаётся). */
    void removeItem(const QString &text, qint64 whenMs = 0);
    /** Снять tombstone (запись снова считается живой). */
    void resurrect(const QString &text);
    void clearTombstones();
    /** Полная замена состояния (применение результата синка). */
    void setState(const QVector<HistoryItem> &items,
                  const QHash<QString, qint64> &tombstones);

private:
    QVector<HistoryItem> m_history;
    QHash<QString, qint64> m_tombstones;   // текст → deleted_at_ms
    int m_maxItems = 32;
    bool m_dirty = false;
    QByteArray m_key;   // AES-256-GCM (32 байта); пусто — без шифрования
};
