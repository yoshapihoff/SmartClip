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

#include <algorithm>

namespace {

/** Стенное время правки (мс). Единственный источник меток для LWW-слияния. */
qint64 nowWallMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

}  // namespace

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

    // Повторное копирование «воскрешает» запись: снимаем tombstone, иначе
    // удаление на другом устройстве могло бы её снова убить при синке.
    if (m_tombstones.contains(text)) {
        m_tombstones.remove(text);
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

// ────────────────────────────── загрузка ──────────────────────────────

namespace {

/** Расшифровать значение поля (v2:...) или декодировать открытый base64. */
QString decodeField(const QString &val, const QByteArray &key, bool *wasPlain)
{
    if (wasPlain)
        *wasPlain = false;
    if (val.startsWith(QLatin1String("v2:"))) {
        const QByteArray blob = QByteArray::fromBase64(val.mid(3).toUtf8());
        bool ok = false;
        const QByteArray plain = Crypto::decrypt(blob, key, &ok);
        return ok ? QString::fromUtf8(plain) : QString();
    }
    if (wasPlain)
        *wasPlain = true;
    return QString::fromUtf8(QByteArray::fromBase64(val.toUtf8()));
}

/** Прочитать key: value из строки. Возвращает false, если строки нет. */
bool splitKeyValue(const QString &line, QString &key, QString &val)
{
    const int idx = line.indexOf(QLatin1Char(':'));
    if (idx <= 0)
        return false;
    key = line.left(idx).trimmed();
    val = line.mid(idx + 1).trimmed();
    return true;
}

}  // namespace

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
    QHash<QString, qint64> loadedTombstones;
    HistoryItem current;
    QString curDelText;
    qint64 curDelAt = 0;
    bool haveCurrent = false;
    bool haveDel = false;
    bool inItem = false;        // внутри "- text_b64:" элемента истории
    bool inDeleted = false;     // внутри "- text_b64:" tombstone
    int section = 0;            // 0 = шапка, 1 = items, 2 = deleted
    bool migrate = false;       // нашли открытые данные → надо пересохранить

    auto flushItem = [&]() {
        if (inItem && !current.text.isEmpty())
            loaded.push_back(current);
        current = HistoryItem{};
        inItem = false;
    };
    auto flushDeleted = [&]() {
        if (inDeleted && !curDelText.isEmpty())
            loadedTombstones.insert(curDelText, curDelAt);
        curDelText.clear();
        curDelAt = 0;
        inDeleted = false;
    };

    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const QString t = line.trimmed();

        // Заголовки секций
        if (!t.startsWith(QLatin1Char('-'))) {
            if (t == QLatin1String("items:")) {
                flushItem();
                flushDeleted();
                section = 1;
                continue;
            }
            if (t == QLatin1String("deleted:")) {
                flushItem();
                flushDeleted();
                section = 2;
                continue;
            }
        }

        if (t.startsWith(QLatin1Char('-'))) {
            if (section == 2) {
                flushDeleted();
                inDeleted = true;
                const QString rest = t.mid(1).trimmed();
                if (!rest.isEmpty()) {
                    QString k, v;
                    if (splitKeyValue(rest, k, v)
                        && k == QLatin1String("text_b64")) {
                        bool plain = false;
                        curDelText = decodeField(v, m_key, &plain);
                        if (plain)
                            migrate = true;
                    }
                }
                continue;
            }
            // items
            flushItem();
            inItem = true;
            haveCurrent = true;
            const QString rest = t.mid(1).trimmed();
            if (!rest.isEmpty()) {
                QString k, v;
                if (splitKeyValue(rest, k, v) && k == QLatin1String("text_b64")) {
                    bool plain = false;
                    current.text = decodeField(v, m_key, &plain);
                    if (plain)
                        migrate = true;
                }
            }
            continue;
        }

        if (section == 2) {
            QString k, v;
            if (!splitKeyValue(t, k, v))
                continue;
            if (k == QLatin1String("text_b64")) {
                bool plain = false;
                curDelText = decodeField(v, m_key, &plain);
                if (plain)
                    migrate = true;
            } else if (k == QLatin1String("deleted_at_ms")) {
                bool ok = false;
                const qint64 x = v.toLongLong(&ok);
                if (ok && x > 0)
                    curDelAt = x;
            }
            continue;
        }

        // items
        if (!inItem)
            continue;
        QString key, val;
        if (!splitKeyValue(t, key, val))
            continue;

        if (key == QLatin1String("text_b64")) {
            bool plain = false;
            current.text = decodeField(val, m_key, &plain);
            if (plain)
                migrate = true;
        } else if (key == QLatin1String("usage_count")) {
            bool ok = false;
            const int v = val.toInt(&ok);
            if (ok && v >= 0)
                current.usageCount = v;
        } else if (key == QLatin1String("added_at_ms")) {
            bool ok = false;
            const qint64 v = val.toLongLong(&ok);
            if (ok && v >= 0)
                current.addedAtMs = v;
        } else if (key == QLatin1String("favorite_color_index")) {
            bool ok = false;
            const int v = val.toInt(&ok);
            if (ok && v >= -1 && v <= 32)
                current.favoriteColorIndex = v;
        } else if (key == QLatin1String("mask_in_menu")) {
            const QString lower = val.toLower();
            current.maskInMenu = (lower == QLatin1String("1")
                                  || lower == QLatin1String("true")
                                  || lower == QLatin1String("yes"));
        } else if (key == QLatin1String("comment_b64")) {
            bool plain = false;
            current.comment = decodeField(val, m_key, &plain);
            if (plain && !current.comment.isEmpty())
                migrate = true;
        } else if (key == QLatin1String("fav_changed_at_ms")) {
            bool ok = false;
            const qint64 v = val.toLongLong(&ok);
            if (ok && v > 0)
                current.favChangedAtMs = v;
        } else if (key == QLatin1String("mask_changed_at_ms")) {
            bool ok = false;
            const qint64 v = val.toLongLong(&ok);
            if (ok && v > 0)
                current.maskChangedAtMs = v;
        } else if (key == QLatin1String("comment_changed_at_ms")) {
            bool ok = false;
            const qint64 v = val.toLongLong(&ok);
            if (ok && v > 0)
                current.commentChangedAtMs = v;
        }
    }

    flushItem();
    flushDeleted();
    Q_UNUSED(haveCurrent);
    Q_UNUSED(haveDel);

    m_history = loaded;
    m_tombstones = loadedTombstones;
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
    auto encodeField = [&](const QString &plain) -> QString {
        if (enc) {
            const QByteArray blob = Crypto::encrypt(plain.toUtf8(), m_key);
            return QLatin1String("v2:") + QString::fromLatin1(blob.toBase64());
        }
        return QString::fromLatin1(plain.toUtf8().toBase64());
    };

    QTextStream out(&f);
    out << "version: " << (enc ? 3 : 1) << "\n";
    out << "items:\n";
    for (const HistoryItem &item : m_history) {
        out << "  - text_b64: " << encodeField(item.text) << "\n";
        out << "    usage_count: " << item.usageCount << "\n";
        out << "    added_at_ms: " << item.addedAtMs << "\n";
        out << "    favorite_color_index: " << item.favoriteColorIndex << "\n";
        out << "    mask_in_menu: " << (item.maskInMenu ? "1" : "0") << "\n";
        // Метки правок — только если поле реально менялось (иначе строка не пишется,
        // отсутствие поля при загрузке читается как 0 → легаси-правило ведущего).
        if (item.favChangedAtMs > 0) {
            out << "    fav_changed_at_ms: " << item.favChangedAtMs << "\n";
        }
        if (item.maskChangedAtMs > 0) {
            out << "    mask_changed_at_ms: " << item.maskChangedAtMs << "\n";
        }
        if (item.commentChangedAtMs > 0) {
            out << "    comment_changed_at_ms: " << item.commentChangedAtMs << "\n";
        }
        if (!item.comment.isEmpty()) {
            out << "    comment_b64: " << encodeField(item.comment) << "\n";
        }
    }

    if (!m_tombstones.isEmpty()) {
        out << "deleted:\n";
        for (auto it = m_tombstones.constBegin(); it != m_tombstones.constEnd(); ++it) {
            if (it.key().isEmpty())
                continue;
            out << "  - text_b64: " << encodeField(it.key()) << "\n";
            out << "    deleted_at_ms: " << it.value() << "\n";
        }
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

void HistoryManager::setFavoriteColor(const QString &text, int colorIndex,
                                     qint64 changedAtMs)
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](HistoryItem &item) {
                              return item.text == text;
                          });
    if (it != m_history.end()) {
        it->favoriteColorIndex = (colorIndex >= -1 && colorIndex <= 32) ? colorIndex : -1;
        // Метка = время правки: явное изменение (добавление/снятие/смена
        // цвета) делает поле «известным» и включает LWW при синке.
        it->favChangedAtMs = (changedAtMs > 0) ? changedAtMs : nowWallMs();
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

void HistoryManager::setMaskInMenu(const QString &text, bool mask, qint64 changedAtMs)
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](HistoryItem &item) {
                              return item.text == text;
                          });
    if (it != m_history.end()) {
        it->maskInMenu = mask;
        it->maskChangedAtMs = (changedAtMs > 0) ? changedAtMs : nowWallMs();
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

void HistoryManager::setComment(const QString &text, const QString &comment,
                                qint64 changedAtMs)
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](HistoryItem &item) {
                              return item.text == text;
                          });
    if (it != m_history.end()) {
        it->comment = comment;
        it->commentChangedAtMs = (changedAtMs > 0) ? changedAtMs : nowWallMs();
        m_dirty = true;
    }
}

QString HistoryManager::comment(const QString &text) const
{
    auto it = std::find_if(m_history.begin(), m_history.end(),
                          [&text](const HistoryItem &item) {
                              return item.text == text;
                          });
    return (it != m_history.end()) ? it->comment : QString();
}

void HistoryManager::clearHistory()
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_history.begin(); it != m_history.end();) {
        if ((*it).favoriteColorIndex == -1) {
            // Tombstone на каждый удалённый элемент: очистка «не избранного»
            // должна доехать до других устройств (требование Лёши).
            m_tombstones.insert((*it).text, nowMs);
            it = m_history.erase(it);
        } else {
            ++it;
        }
    }
    m_dirty = true;
}

// ────────────────────────── удаление / воскрешение ──────────────────────────

void HistoryManager::removeItem(const QString &text, qint64 whenMs)
{
    if (text.isEmpty())
        return;
    const qint64 ts = whenMs > 0 ? whenMs : QDateTime::currentMSecsSinceEpoch();
    m_tombstones.insert(text, ts);
    for (auto it = m_history.begin(); it != m_history.end(); ++it) {
        if (it->text == text) {
            m_history.erase(it);
            break;
        }
    }
    m_dirty = true;
}

void HistoryManager::resurrect(const QString &text)
{
    if (m_tombstones.remove(text) > 0)
        m_dirty = true;
}

void HistoryManager::clearTombstones()
{
    if (!m_tombstones.isEmpty()) {
        m_tombstones.clear();
        m_dirty = true;
    }
}

void HistoryManager::setState(const QVector<HistoryItem> &items,
                              const QHash<QString, qint64> &tombstones)
{
    m_history = items;
    m_tombstones = tombstones;
    sortHistory();
    trimToMaxItems();
    m_dirty = true;
}
