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
        bool isFavorite = false;
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
    void saveHistory(const QString &filePath) const;
    
    // Methods for favorites
    void toggleFavorite(const QString &text);
    bool isFavorite(const QString &text) const;
    void sortHistory();
    
    // Method for usage count
    void incrementUsageCount(const QString &text);
    
    // Method to clear history
    void clearHistory();

private:
    QVector<HistoryItem> m_history;
    int m_maxItems = 20;
    bool m_dirty = false;
};
