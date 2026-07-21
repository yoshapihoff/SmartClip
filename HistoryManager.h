#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QDateTime>

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
    void loadHistory(const QString &filePath);
    /** Load history and trim to at most \a maxItemsForTrim (if > 0). Use when loading at startup so limit from settings is applied. */
    void loadHistory(const QString &filePath, int maxItemsForTrim);
    void saveHistory(const QString &filePath) const;
    
    // Methods for favorites
    void toggleFavorite(const QString &text);
    bool isFavorite(const QString &text) const;
    void setFavoriteColor(const QString &text, int colorIndex);
    int favoriteColorIndex(const QString &text) const;
    void sortHistory();
    
    // Method for usage count
    void incrementUsageCount(const QString &text);

    // Mask in menu (encrypted display)
    void setMaskInMenu(const QString &text, bool mask);
    bool maskInMenu(const QString &text) const;

    // Method to clear history
    void clearHistory();

private:
    QVector<HistoryItem> m_history;
    int m_maxItems = 32;
    bool m_dirty = false;
};
