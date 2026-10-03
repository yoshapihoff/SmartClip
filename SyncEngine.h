#pragma once

#include "HistoryManager.h"

#include <QVector>
#include <QHash>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────
// SyncEngine — чистое (без сети) ядро слияния состояния истории.
//
// Семантика (по договорённости с Лёшей, 2026-10-03):
//  * Ключ записи — ТЕКСТ.
//  * usageCount — берём МАКСИМУМ из двух версий.
//  * Избранное: избранные ведущего сохраняют свои цвета как есть; избранные
//    ведомого тоже остаются избранными, но получают следующие свободные цвета.
//    Всего поддерживается 32 избранных (цвета 0..31); «лишние» теряют пометку.
//  * mask (скрытие пароля) и comment — приоритет у ВЕДУЩЕГО; если у ведущего
//    комментарий пустой, а у ведомого есть — берём комментарий ведомого.
//  * Удаления — через tombstone'ы; повторное появление записи (позже времени
//    удаления) «воскрешает» её.
//  * Порядок — как в HistoryManager::sortHistory: избранные → usage desc →
//    added desc → текст.
//  * После слияния список обрезается до historySize правилом trimToMaxItems
//    (неизбранные → меньший usage → старые удаляются первыми).
// ─────────────────────────────────────────────────────────────────────────
namespace SyncEngine {

constexpr int kMaxFavoriteColors = 32;

struct NetworkState {
    QVector<HistoryManager::HistoryItem> items;
    QHash<QString, qint64> tombstones;   // текст → deleted_at_ms
};

/** Слияние локального и удалённого состояния. localIsMaster задаёт, чья
 *  версия приоритетна. historySize — лимит истории (после слияния). */
NetworkState merge(const NetworkState &local,
                   const NetworkState &remote,
                   bool localIsMaster,
                   int historySize,
                   int maxFavoriteColors = kMaxFavoriteColors);

}  // namespace SyncEngine
