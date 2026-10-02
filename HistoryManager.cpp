#include "HistoryManager.h"
#include "Crypto.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QByteArray>
#include <QDateTime>
#include <QDebug>
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
    sortHistory(); // Сортируем после добавления
    m_dirty = true;
}

void HistoryManager::trimToMaxItems()
{
    while (m_history.size() > m_maxItems) {
        int removeIndex = 0;
        for (int i = 1; i < m_history.size(); ++i) {
            const HistoryItem &cand = m_history.at(i);
            const HistoryItem &current = m_history.at(removeIndex);
            // Приоритет удаления: неизбранные → с меньшим числом обращений → самые старые по времени добавления
            const bool candFavorite = (cand.favoriteColorIndex != -1);
            const bool currentFavorite = (current.favoriteColorIndex != -1);
            const bool candIsWorseToKeep =
                (!candFavorite && currentFavorite)
                || (candFavorite == currentFavorite && cand.usageCount < current.usageCount)
                || (candFavorite == currentFavorite && cand.usageCount == current.usageCount && cand.addedAtMs < current.addedAtMs);
            if (candIsWorseToKeep) {
                removeIndex = i;
            }
        }
        m_history.removeAt(removeIndex);
    }
}

bool HistoryManager::loadHistory(const QString &filePath)
{
    QFile f(filePath);
    if (!f.exists()) {
        return false;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QVector<HistoryItem> loaded;
    HistoryItem current;
    bool inItem = false;
    bool migrate = false;   // нашли открытые данные → надо пересохранить шифр.

    auto parseKeyValue = [&](const QString &line) {
        const int idx = line.indexOf(QLatin1Char(':'));
        if (idx <= 0) {
            return;
        }

        const QString key = line.left(idx).trimmed();
        const QString val = line.mid(idx + 1).trimmed();

        if (key == QLatin1String("text_b64")) {
            if (val.startsWith(QLatin1String("v2:"))) {
                // зашифрованный элемент: v2:<base64(nonce||ct||tag)>
                const QByteArray blob =
                    QByteArray::fromBase64(val.mid(3).toUtf8());
                bool ok = false;
                const QByteArray plain = Crypto::decrypt(blob, m_key, &ok);
                current.text = ok ? QString::fromUtf8(plain) : QString();
                if (!ok)
                    qWarning() << "SmartClip: не расшифровал элемент истории"
                               << "(нет ключа?)";
            } else {
                // старый ОТКРЫТЫЙ формат — декодируем и помечаем миграцию
                current.text = QString::fromUtf8(
                    QByteArray::fromBase64(val.toUtf8()));
                migrate = true;
            }
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
        } else if (key == QLatin1String("favorite_color_index")) {
            bool ok = false;
            const int v = val.toInt(&ok);
            if (ok && v >= -1 && v <= 32) {
                current.favoriteColorIndex = v;
            }
        } else if (key == QLatin1String("mask_in_menu")) {
            const QString lower = val.toLower();
            current.maskInMenu = (lower == QLatin1String("1") || lower == QLatin1String("true") || lower == QLatin1String("yes"));
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
    sortHistory(); // Сортируем после загрузки
    trimToMaxItems();
    // Миграция: файл был в открытом виде и есть ключ → перезапишем шифром.
    // Без ключа открытый формат остаётся как есть (мигрировать некуда).
    m_dirty = migrate && encryptionEnabled();
    return m_dirty;
}

void HistoryManager::loadHistory(const QString &filePath, int maxItemsForTrim)
{
    if (maxItemsForTrim > 0) {
        m_maxItems = maxItemsForTrim;
    }
    loadHistory(filePath);
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

    const bool enc = encryptionEnabled() && Crypto::available();
    QTextStream out(&f);
    out << "version: " << (enc ? 2 : 1) << "\n";
    out << "items:\n";
    for (const HistoryItem &item : m_history) {
        QString payload;
        if (enc) {
            const QByteArray blob = Crypto::encrypt(item.text.toUtf8(), m_key);
            payload = QLatin1String("v2:")
                      + QString::fromLatin1(blob.toBase64());
        } else {
            payload = QString::fromLatin1(item.text.toUtf8().toBase64());
        }
        out << "  - text_b64: " << payload << "\n";
        out << "    usage_count: " << item.usageCount << "\n";
        out << "    added_at_ms: " << item.addedAtMs << "\n";
        out << "    favorite_color_index: " << item.favoriteColorIndex << "\n";
        out << "    mask_in_menu: " << (item.maskInMenu ? "1" : "0") << "\n";
    }
}

void HistoryManager::toggleFavorite(const QString &text)
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](HistoryItem &item) {
                              return item.text == text;
                          });
    if (it != m_history.end()) {
        if (it->favoriteColorIndex != -1) {
            it->favoriteColorIndex = -1;
        } else {
            it->favoriteColorIndex = 0; // временно; SmartClipApp вызовет setFavoriteColor с нужным цветом
        }
        m_dirty = true;
        sortHistory(); // Пересортировываем после изменения
    }
}

bool HistoryManager::isFavorite(const QString &text) const
{
    return favoriteColorIndex(text) != -1;
}

void HistoryManager::setFavoriteColor(const QString &text, int colorIndex)
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](HistoryItem &item) {
                              return item.text == text;
                          });
    if (it != m_history.end()) {
        it->favoriteColorIndex = (colorIndex >= -1 && colorIndex <= 32) ? colorIndex : -1;
        m_dirty = true;
    }
}

int HistoryManager::favoriteColorIndex(const QString &text) const
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](const HistoryItem &item) {
                              return item.text == text;
                          });
    return (it != m_history.end()) ? it->favoriteColorIndex : -1;
}

void HistoryManager::sortHistory()
{
    std::sort(m_history.begin(), m_history.end(), [](const HistoryItem &a, const HistoryItem &b) {
        // Сначала избранные элементы (favoriteColorIndex != -1)
        const bool aFav = (a.favoriteColorIndex != -1);
        const bool bFav = (b.favoriteColorIndex != -1);
        if (aFav != bFav) {
            return aFav > bFav;
        }
        // Затем по количеству использований
        if (a.usageCount != b.usageCount) {
            return a.usageCount > b.usageCount;
        }
        // Затем по дате создания
        if (a.addedAtMs != b.addedAtMs) {
            return a.addedAtMs > b.addedAtMs;
        }
        return a.text < b.text;
    });
}

void HistoryManager::incrementUsageCount(const QString &text)
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](HistoryItem &item) {
                              return item.text == text;
                          });
    if (it != m_history.end()) {
        it->usageCount++;
        m_dirty = true;
        sortHistory(); // Пересортировываем после изменения счетчика
    }
}

void HistoryManager::setMaskInMenu(const QString &text, bool mask)
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](HistoryItem &item) {
                              return item.text == text;
                          });
    if (it != m_history.end()) {
        it->maskInMenu = mask;
        m_dirty = true;
    }
}

bool HistoryManager::maskInMenu(const QString &text) const
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](const HistoryItem &item) {
                              return item.text == text;
                          });
    return (it != m_history.end()) ? it->maskInMenu : false;
}

void HistoryManager::clearHistory()
{
    for (auto it = m_history.begin(); it != m_history.end();) {
        if ((*it).favoriteColorIndex == -1) {
            it = m_history.erase(it);
        } else {
            ++it;
        }
    }
    m_dirty = true;
}