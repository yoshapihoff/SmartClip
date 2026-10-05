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
//  * usageCount — берём МАКСИМУМ из двух версий; addedAtMs — максимум.
//  * mask/comment/favorite — Last-Writer-Wins по МЕТКАМ ВРЕМЕНИ ПРАВКИ
//    (favChangedAtMs/maskChangedAtMs/commentChangedAtMs). Метка 0 = поле
//    никогда не меняли → старое правило «приоритет ведущего» (пустой
//    комментарий не затирает непустой; mask — ведущего; избранное —
//    ОБЪЕДИНЕНИЕ, цвета приоритетно у ведущего). При равных ненулевых
//    метках побеждает ведущий (детерминированно). LWW на избранном решает
//    только «в избранном/нет»; конкретный ЦВЕТ раздаётся централизованно
//    (ведущий первым, ведомым — следующие свободные цвета 0..31).
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
