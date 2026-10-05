#include "SyncEngine.h"

#include <QSet>
#include <algorithm>

namespace SyncEngine {

namespace {

/** LWW по метке времени: 0 = «никогда не менялось». При равных метках
 *  (в т.ч. 0/0) побеждает сторона A — ведущий. */
bool lwwTakeA(qint64 aTs, qint64 bTs)
{
    if (aTs == bTs)
        return true;      // ничья → ведущий
    return aTs > bTs;
}

struct FavPick {
    bool favorite = false;    // в избранном после LWW?
    bool masterSide = true;   // «владелец» избранного — ведущий?
};

/** Решение по избранному. Легаси-случай (обе метки 0, поле никто не менял) —
 *  ОБЪЕДИНЕНИЕ: избранное любой стороны живо, цвет приоритетно у ведущего.
 *  Иначе — честный LWW по метке правки (снятие избранного доезжает). */
FavPick favPick(const HistoryManager::HistoryItem &master,
                const HistoryManager::HistoryItem &slave)
{
    const qint64 mt = master.favChangedAtMs;
    const qint64 st = slave.favChangedAtMs;
    FavPick p;
    if (mt == 0 && st == 0) {
        p.favorite = (master.favoriteColorIndex >= 0)
                     || (slave.favoriteColorIndex >= 0);
        p.masterSide = (master.favoriteColorIndex >= 0);
        return p;
    }
    const bool masterWins = (mt >= st);   // при равенстве (>0) — ведущий
    const HistoryManager::HistoryItem &w = masterWins ? master : slave;
    p.favorite = (w.favoriteColorIndex >= 0);
    p.masterSide = masterWins;
    return p;
}

/** Слияние mask/comment/favorite-полей: LWW по метке ВРЕМЕНИ правки, а при
 *  метке 0 (поле никогда не меняли) — старое правило «приоритет ведущего». */
void mergeFields(HistoryManager::HistoryItem &chosen,
                 const HistoryManager::HistoryItem &master,
                 const HistoryManager::HistoryItem &slave)
{
    // ── mask (скрытие пароля) ──
    if (master.maskChangedAtMs != slave.maskChangedAtMs)
        chosen.maskInMenu = lwwTakeA(master.maskChangedAtMs, slave.maskChangedAtMs)
                                ? master.maskInMenu : slave.maskInMenu;
    else
        chosen.maskInMenu = master.maskInMenu;   // ничья (в т.ч. 0/0) → ведущий
    chosen.maskChangedAtMs = qMax(master.maskChangedAtMs, slave.maskChangedAtMs);

    // ── comment ──
    if (master.commentChangedAtMs != slave.commentChangedAtMs) {
        chosen.comment = lwwTakeA(master.commentChangedAtMs, slave.commentChangedAtMs)
                             ? master.comment : slave.comment;
    } else if (master.commentChangedAtMs == 0) {
        // Никто ещё не менял: пустой комментарий ведущего НЕ затирает
        // непустой ведомого (легаси-правило).
        chosen.comment = !master.comment.isEmpty() ? master.comment : slave.comment;
    } else {
        chosen.comment = master.comment;   // обе метки равны и >0 → ведущий
    }
    chosen.commentChangedAtMs = qMax(master.commentChangedAtMs,
                                     slave.commentChangedAtMs);

    // Метка избранного (сам colorIndex раздаётся централизованно, шаг 4) —
    // нужна, чтобы знание о правке транслировалось дальше по цепочке.
    chosen.favChangedAtMs = qMax(master.favChangedAtMs, slave.favChangedAtMs);
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
    QHash<QString, bool> favWanted;       // текст → должен быть избранным (LWW)
    QHash<QString, bool> favOwnerMaster;  // текст → «владелец» избранного — ведущий

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
        bool wantedFav = false;
        bool ownerMaster = true;
        if (mi && si) {
            // Запись есть с обеих сторон: usage — максимум, время — максимум
            // (для порядка «новые сверху»); mask/comment/favorite — LWW.
            chosen = *mi;
            chosen.usageCount = qMax(mi->usageCount, si->usageCount);
            chosen.addedAtMs = qMax(mi->addedAtMs, si->addedAtMs);
            mergeFields(chosen, *mi, *si);
            const FavPick fp = favPick(*mi, *si);
            wantedFav = fp.favorite;
            ownerMaster = fp.masterSide;
        } else if (mi) {
            chosen = *mi;
            wantedFav = chosen.favoriteColorIndex >= 0;
            ownerMaster = true;
        } else {
            chosen = *si;
            wantedFav = chosen.favoriteColorIndex >= 0;
            ownerMaster = false;
        }

        if (del > 0 && del >= chosen.addedAtMs) {
            // Удаление свежее (или одновременно) появления — запись мертва.
            tombstones.insert(t, del);
        } else {
            // Конкретный ЦВЕТ избранного раздаётся централизованно (шаг 4);
            // здесь только помечаем «в избранном» через placeholder >= 0.
            chosen.favoriteColorIndex = wantedFav ? 0 : -1;
            favWanted.insert(t, wantedFav);
            favOwnerMaster.insert(t, ownerMaster);
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

    // 4) Назначаем конкретные ЦВЕТА избранного централизованно: сначала
    //    «владельцы»-избранные ведущего (в порядке ведущего, цвета сохраняем),
    //    затем остальные — им достаются следующие свободные цвета.
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

    // 4a) Избранные, где LWW-победу одержал ВЕДУЩИЙ, — в порядке ведущего
    //     (их цвета стараемся сохранить как есть).
    for (const auto &it : masterSide.items) {
        if (!aliveSet.contains(it.text))
            continue;
        if (!favWanted.value(it.text, false))
            continue;
        if (!favOwnerMaster.value(it.text, true))
            continue;
        assignMasterFav(it);
    }
    // 4b) Остальные избранные (LWW-победа ведомого либо односторонние
    //     записи ведомого) — получают следующие свободные цвета.
    for (const QString &t : order) {
        if (!aliveSet.contains(t))
            continue;
        if (!favWanted.value(t, false))
            continue;
        if (favAssigned.contains(t))
            continue;
        const HistoryManager::HistoryItem *si = slaveItems.value(t, nullptr);
        const HistoryManager::HistoryItem *mi = masterItems.value(t, nullptr);
        const HistoryManager::HistoryItem *src = si ? si : mi;
        if (!src)
            continue;
        assignSlaveFav(*src);
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
