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

private:
    QVector<HistoryItem> m_history;
    int m_maxItems = 20;
    bool m_dirty = false;
};
