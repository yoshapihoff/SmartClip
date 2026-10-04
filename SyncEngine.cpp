#include "SyncEngine.h"

#include <QSet>
#include <algorithm>

namespace SyncEngine {

namespace {

struct Picked {
    HistoryManager::HistoryItem item;
    bool favorite = false;
};

bool betterUsage(const HistoryManager::HistoryItem &a,
                 const HistoryManager::HistoryItem &b)
{
    if (a.usageCount != b.usageCount)
        return a.usageCount > b.usageCount;
    if (a.addedAtMs != b.addedAtMs)
        return a.addedAtMs > b.addedAtMs;
    return false;
}

/** Поле из выигравшей версии + приоритеты для mask/comment. */
void mergeFields(HistoryManager::HistoryItem &chosen,
                 const HistoryManager::HistoryItem &master,
                 const HistoryManager::HistoryItem &slave)
{
    // mask — приоритет ведущего
    chosen.maskInMenu = master.maskInMenu;
    // comment — приоритет ведущего, НО пустой не затирает непустой ведомого
    chosen.comment = !master.comment.isEmpty() ? master.comment : slave.comment;
}

}  // namespace

NetworkState merge(const NetworkState &local, const NetworkState &remote,
                   bool localIsMaster, int historySize, int maxFavoriteColors)
{
    const NetworkState &masterSide = localIsMaster ? local : remote;
    const NetworkState &slaveSide = localIsMaster ? remote : local;

    // 1) Объединённый набор ключей: история (обе стороны) + tombstone'ы.
    QVector<QString> order;
    QSet<QString> seen;
    QHash<QString, const HistoryManager::HistoryItem *> masterItems, slaveItems;

    auto addKey = [&](const QString &t) {
        if (t.isEmpty() || seen.contains(t))
            return;
        seen.insert(t);
        order.push_back(t);
    };
    for (const auto &it : masterSide.items) {
        masterItems.insert(it.text, &it);
        addKey(it.text);
    }
    for (const auto &it : slaveSide.items) {
        slaveItems.insert(it.text, &it);
        addKey(it.text);
    }
    for (auto it = masterSide.tombstones.constBegin();
         it != masterSide.tombstones.constEnd(); ++it)
        addKey(it.key());
    for (auto it = slaveSide.tombstones.constBegin();
         it != slaveSide.tombstones.constEnd(); ++it)
        addKey(it.key());

    // 2) Отбираем живые записи (учитывая tombstone'ы).
    QVector<HistoryManager::HistoryItem> alive;
    QHash<QString, qint64> tombstones;

    auto tombAt = [&](const QString &t) -> qint64 {
        const bool lm = masterSide.tombstones.contains(t);
        const bool ls = slaveSide.tombstones.contains(t);
        if (lm && ls)
            return qMax(masterSide.tombstones.value(t), slaveSide.tombstones.value(t));
        if (lm)
            return masterSide.tombstones.value(t);
        if (ls)
            return slaveSide.tombstones.value(t);
        return 0;
    };

    for (const QString &t : order) {
        const HistoryManager::HistoryItem *mi = masterItems.value(t, nullptr);
        const HistoryManager::HistoryItem *si = slaveItems.value(t, nullptr);
        const qint64 del = tombAt(t);

        // Ключ может присутствовать ТОЛЬКО как tombstone (запись удалена с
        // обеих сторон и её уже нет ни в одной истории). Раньше здесь был
        // `chosen = *si` при si == nullptr → SIGSEGV в QString::operator=.
        if (!mi && !si) {
            if (del > 0)
                tombstones.insert(t, del);
            continue;
        }

        HistoryManager::HistoryItem chosen;
        if (mi && si) {
            // Запись есть с обеих сторон: поля — из ведущего (приоритет),
            // usage — максимум, время — максимум (для порядка «новые сверху»).
            chosen = *mi;
            chosen.usageCount = qMax(mi->usageCount, si->usageCount);
            chosen.addedAtMs = qMax(mi->addedAtMs, si->addedAtMs);
            mergeFields(chosen, *mi, *si);
        } else if (mi) {
            chosen = *mi;
        } else {
            chosen = *si;
        }

        if (del > 0 && del >= chosen.addedAtMs) {
            // Удаление свежее (или одновременно) появления — запись мертва.
            tombstones.insert(t, del);
        } else {
            alive.push_back(chosen);
            if (del > 0)
                tombstones.insert(t, del);   // старый tombstone сохраняем как есть
        }
    }

    // 3) Ранжирование по usage (как sortHistory: usage desc → added desc → текст)
    std::stable_sort(alive.begin(), alive.end(),
                     [](const HistoryManager::HistoryItem &a,
                        const HistoryManager::HistoryItem &b) {
                         if (a.usageCount != b.usageCount)
                             return a.usageCount > b.usageCount;
                         if (a.addedAtMs != b.addedAtMs)
                             return a.addedAtMs > b.addedAtMs;
                         return a.text < b.text;
                     });

    // 4) Назначаем избранное/цвета по приоритету: сначала избранные ведущего
    //    (в порядке их следования у ведущего, чтобы цвета сохранились),
    //    затем избранные ведомого (следующие свободные цвета).
    QSet<QString> aliveSet;
    for (const auto &it : alive)
        aliveSet.insert(it.text);

    QSet<int> used;
    QSet<QString> favAssigned;

    auto assignMasterFav = [&](const HistoryManager::HistoryItem &src) {
        if (src.favoriteColorIndex < 0)
            return;
        int color = src.favoriteColorIndex;
        if (color == 32 || used.contains(color)) {
            // Нет гарантии уникальности (напр. белый 32) → берём свободный,
            // сохраняя приоритет «избранный остаётся избранным».
            color = -1;
            for (int c = 0; c < maxFavoriteColors; ++c)
                if (!used.contains(c)) { color = c; break; }
            if (color < 0)
                return;   // лимит избранного исчерпан
        }
        used.insert(color);
        favAssigned.insert(src.text);
        for (auto &it : alive)
            if (it.text == src.text)
                it.favoriteColorIndex = color;
    };
    auto assignSlaveFav = [&](const HistoryManager::HistoryItem &src) {
        if (src.favoriteColorIndex < 0)
            return;
        int color = -1;
        for (int c = 0; c < maxFavoriteColors; ++c)
            if (!used.contains(c)) { color = c; break; }
        if (color < 0)
            return;   // лимит избранного исчерпан → без пометки
        used.insert(color);
        favAssigned.insert(src.text);
        for (auto &it : alive)
            if (it.text == src.text)
                it.favoriteColorIndex = color;
    };

    // Избранное ведущего — в порядке следования у ведущего.
    for (const auto &it : masterSide.items) {
        if (!aliveSet.contains(it.text))
            continue;
        if (it.favoriteColorIndex >= 0)
            assignMasterFav(it);
    }
    // Избранное ведомого — порядок задаёт порядок в объединённом списке.
    for (const QString &t : order) {
        if (!aliveSet.contains(t))
            continue;
        const HistoryManager::HistoryItem *si = slaveItems.value(t, nullptr);
        if (!si || si->favoriteColorIndex < 0)
            continue;
        if (favAssigned.contains(t))
            continue;
        assignSlaveFav(*si);
    }

    // 5) Снимаем пометку со всего, что превысило лимит избранного.
    int favCount = 0;
    for (const auto &it : alive)
        if (it.favoriteColorIndex >= 0)
            ++favCount;
    if (favCount > maxFavoriteColors) {
        // Оставляем первые maxFavoriteColors по цвету, остальным — без избранного.
        QSet<int> keep;
        QVector<int> colors;
        for (const auto &it : alive)
            if (it.favoriteColorIndex >= 0)
                colors.push_back(it.favoriteColorIndex);
        std::sort(colors.begin(), colors.end());
        for (int i = 0; i < maxFavoriteColors && i < colors.size(); ++i)
            keep.insert(colors[i]);
        for (auto &it : alive) {
            if (it.favoriteColorIndex >= 0 && !keep.contains(it.favoriteColorIndex))
                it.favoriteColorIndex = -1;
        }
    }

    // 6) Обрезка по лимиту истории (неизбранные → меньший usage → старые).
    int limit = historySize > 0 ? historySize : alive.size();
    while (alive.size() > limit) {
        int removeIndex = 0;
        for (int i = 1; i < alive.size(); ++i) {
            const auto &cand = alive.at(i);
            const auto &cur = alive.at(removeIndex);
            const bool candFav = cand.favoriteColorIndex != -1;
            const bool curFav = cur.favoriteColorIndex != -1;
            const bool worse =
                (!candFav && curFav)
                || (candFav == curFav && cand.usageCount < cur.usageCount)
                || (candFav == curFav && cand.usageCount == cur.usageCount
                    && cand.addedAtMs < cur.addedAtMs);
            if (worse)
                removeIndex = i;
        }
        alive.removeAt(removeIndex);
    }

    // 7) Финальный порядок — как в HistoryManager::sortHistory.
    std::stable_sort(alive.begin(), alive.end(),
                     [](const HistoryManager::HistoryItem &a,
                        const HistoryManager::HistoryItem &b) {
                         const bool aFav = a.favoriteColorIndex != -1;
                         const bool bFav = b.favoriteColorIndex != -1;
                         if (aFav != bFav)
                             return aFav > bFav;
                         if (a.usageCount != b.usageCount)
                             return a.usageCount > b.usageCount;
                         if (a.addedAtMs != b.addedAtMs)
                             return a.addedAtMs > b.addedAtMs;
                         return a.text < b.text;
                     });

    NetworkState out;
    out.items = alive;
    out.tombstones = tombstones;
    return out;
}

}  // namespace SyncEngine
