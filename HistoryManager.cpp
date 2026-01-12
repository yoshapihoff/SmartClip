#include "HistoryManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QRegularExpression>
#include <QByteArray>
#include <algorithm>

HistoryManager::HistoryManager(QObject *parent)
    : QObject(parent)
{
}

const QVector<HistoryManager::HistoryItem> &HistoryManager::history() const
{
    return m_history;
}

int HistoryManager::maxItems() const
{
    return m_maxItems;
}

void HistoryManager::setMaxItems(int maxItems)
{
    if (m_maxItems != maxItems) {
        m_maxItems = maxItems;
        trimToMaxItems();
        m_dirty = true;
    }
}

bool HistoryManager::isDirty() const
{
    return m_dirty;
}

void HistoryManager::clearDirty()
{
    m_dirty = false;
}

void HistoryManager::addToHistory(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    auto it = std::find_if(m_history.begin(), m_history.end(), [&text](const HistoryItem &item) {
        return item.text == text;
    });

    if (it == m_history.end()) {
        HistoryItem item;
        item.text = text;
        item.usageCount = 0;
        item.addedAtMs = nowMs;
        m_history.push_back(item);
    } else {
        it->addedAtMs = nowMs;
    }

    trimToMaxItems();
    m_dirty = true;
}

void HistoryManager::trimToMaxItems()
{
    while (m_history.size() > m_maxItems) {
        int oldestIndex = 0;
        qint64 oldestTs = m_history.at(0).addedAtMs;
        for (int i = 1; i < m_history.size(); ++i) {
            const qint64 ts = m_history.at(i).addedAtMs;
            if (ts < oldestTs) {
                oldestTs = ts;
                oldestIndex = i;
            }
        }
        m_history.removeAt(oldestIndex);
    }
}

void HistoryManager::loadHistory(const QString &filePath)
{
    QFile f(filePath);
    if (!f.exists()) {
        return;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QVector<HistoryItem> loaded;
    HistoryItem current;
    bool inItem = false;

    auto parseKeyValue = [&current](const QString &line) {
        const int idx = line.indexOf(QLatin1Char(':'));
        if (idx <= 0) {
            return;
        }

        const QString key = line.left(idx).trimmed();
        const QString val = line.mid(idx + 1).trimmed();

        if (key == QLatin1String("text_b64")) {
            current.text = QString::fromUtf8(QByteArray::fromBase64(val.toUtf8()));
        } else if (key == QLatin1String("usage_count")) {
            bool ok = false;
            const int v = val.toInt(&ok);
            if (ok && v >= 0) {
                current.usageCount = v;
            }
        } else if (key == QLatin1String("added_at_ms")) {
            bool ok = false;
            const qint64 v = val.toLongLong(&ok);
            if (ok && v >= 0) {
                current.addedAtMs = v;
            }
        }
    };

    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QString t = line.trimmed();

        if (t.startsWith(QLatin1Char('-'))) {
            if (inItem && !current.text.isEmpty()) {
                loaded.push_back(current);
            }
            current = HistoryItem{};
            inItem = true;

            const QString rest = t.mid(1).trimmed();
            if (!rest.isEmpty()) {
                parseKeyValue(rest);
            }
            continue;
        }

        if (!inItem) {
            continue;
        }

        parseKeyValue(t);
    }

    if (inItem && !current.text.isEmpty()) {
        loaded.push_back(current);
    }

    m_history = loaded;
    trimToMaxItems();
    m_dirty = false;
}

void HistoryManager::saveHistory(const QString &filePath) const
{
    const QFileInfo fi(filePath);
    if (!fi.dir().exists()) {
        QDir().mkpath(fi.dir().absolutePath());
    }

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    QTextStream out(&f);
    out << "version: 1\n";
    out << "items:\n";
    for (const HistoryItem &item : m_history) {
        const QByteArray b64 = item.text.toUtf8().toBase64();
        out << "  - text_b64: " << b64 << "\n";
        out << "    usage_count: " << item.usageCount << "\n";
        out << "    added_at_ms: " << item.addedAtMs << "\n";
    }
}
