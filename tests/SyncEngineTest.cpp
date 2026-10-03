#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "../SyncEngine.h"
#include "../HistoryManager.h"
#include "../SettingsManager.h"

using HistoryItem = HistoryManager::HistoryItem;

namespace {

HistoryItem mk(const QString &text, int usage, qint64 added, int color = -1,
               bool mask = false, const QString &comment = QString())
{
    HistoryItem it;
    it.text = text;
    it.usageCount = usage;
    it.addedAtMs = added;
    it.favoriteColorIndex = color;
    it.maskInMenu = mask;
    it.comment = comment;
    return it;
}

}  // namespace

class TestSyncEngine : public QObject
{
    Q_OBJECT

private slots:
    // ── SyncEngine ──
    void testUsageTakesMax();
    void testMasterCommentPriority();
    void testMasterMaskPriority();
    void testSlaveFavoritesGetNextColors();
    void testFavoriteLimit32();
    void testHistorySizeTrim();
    void testDeletionPropagates();
    void testResurrectAfterDelete();
    void testOrderingFavoritesThenUsage();

    // ── HistoryManager (tombstones) ──
    void testRemoveItemCreatesTombstone();
    void testAddToHistoryResurrects();

    // ── SettingsManager (сеть) ──
    void testNetworkSettingsRoundTrip();
};

void TestSyncEngine::testUsageTakesMax()
{
    SyncEngine::NetworkState local, remote;
    local.items = {mk("a", 5, 100)};
    remote.items = {mk("a", 9, 100)};
    const auto r = SyncEngine::merge(local, remote, true, 32);
    QCOMPARE(r.items.size(), 1);
    QCOMPARE(r.items.first().usageCount, 9);

    const auto r2 = SyncEngine::merge(local, remote, false, 32);
    QCOMPARE(r2.items.first().usageCount, 9);   // не зависит от роли
}

void TestSyncEngine::testMasterCommentPriority()
{
    // У ведущего непустой комментарий → берём его
    {
        SyncEngine::NetworkState local, remote;
        local.items = {mk("a", 1, 100, -1, false, "master-comment")};
        remote.items = {mk("a", 1, 100, -1, false, "slave-comment")};
        const auto r = SyncEngine::merge(local, remote, true, 32);
        QCOMPARE(r.items.first().comment, QString("master-comment"));
    }
    // У ведущего пустой → берём комментарий ведомого
    {
        SyncEngine::NetworkState local, remote;
        local.items = {mk("a", 1, 100, -1, false, "")};
        remote.items = {mk("a", 1, 100, -1, false, "slave-comment")};
        const auto r = SyncEngine::merge(local, remote, true, 32);
        QCOMPARE(r.items.first().comment, QString("slave-comment"));
    }
    // Роль ведомого: приоритет всё равно у ведущего (тот же результат)
    {
        SyncEngine::NetworkState local, remote;
        local.items = {mk("a", 1, 100, -1, false, "slave-side")};
        remote.items = {mk("a", 1, 100, -1, false, "master-side")};
        const auto r = SyncEngine::merge(local, remote, false, 32);
        QCOMPARE(r.items.first().comment, QString("master-side"));
    }
}

void TestSyncEngine::testMasterMaskPriority()
{
    SyncEngine::NetworkState local, remote;
    local.items = {mk("a", 1, 100, -1, true)};    // мастер: скрыто
    remote.items = {mk("a", 1, 100, -1, false)};  // ведомый: открыто
    const auto r = SyncEngine::merge(local, remote, true, 32);
    QCOMPARE(r.items.first().maskInMenu, true);
}

void TestSyncEngine::testSlaveFavoritesGetNextColors()
{
    SyncEngine::NetworkState local, remote;
    // Мастер: два избранных с цветами 0 и 2
    local.items = {mk("m0", 1, 100, 0), mk("m2", 1, 100, 2)};
    // Ведомый: один избранный (цвет 0 — конфликт) + обычный
    remote.items = {mk("s0", 1, 100, 0), mk("plain", 1, 100)};

    const auto r = SyncEngine::merge(local, remote, true, 32);

    auto colorOf = [&](const QString &t) {
        for (const auto &it : r.items)
            if (it.text == t)
                return it.favoriteColorIndex;
        return -999;
    };
    QCOMPARE(colorOf("m0"), 0);
    QCOMPARE(colorOf("m2"), 2);
    // Ведомый сохранил избранное, но получил первый свободный цвет (1)
    QCOMPARE(colorOf("s0"), 1);
    QCOMPARE(colorOf("plain"), -1);
}

void TestSyncEngine::testFavoriteLimit32()
{
    SyncEngine::NetworkState local, remote;
    // 20 избранных у мастера + 20 у ведомого = 40 > 32
    for (int i = 0; i < 20; ++i)
        local.items.push_back(mk(QString("m%1").arg(i), 1, 100 + i, i));
    for (int i = 0; i < 20; ++i)
        remote.items.push_back(mk(QString("s%1").arg(i), 1, 100 + i, i));

    const auto r = SyncEngine::merge(local, remote, true, 1000);

    int favCount = 0;
    for (const auto &it : r.items)
        if (it.favoriteColorIndex >= 0)
            ++favCount;
    QCOMPARE(favCount, 32);

    // Все избранные мастера (20) остались избранными
    for (int i = 0; i < 20; ++i) {
        bool found = false;
        for (const auto &it : r.items)
            if (it.text == QString("m%1").arg(i) && it.favoriteColorIndex >= 0)
                found = true;
        QVERIFY2(found, "мастерский избранный должен сохранить пометку");
    }
}

void TestSyncEngine::testHistorySizeTrim()
{
    SyncEngine::NetworkState local, remote;
    for (int i = 0; i < 50; ++i)
        local.items.push_back(mk(QString("m%1").arg(i), i, 100 + i));
    const auto r = SyncEngine::merge(local, remote, true, 32);
    QCOMPARE(r.items.size(), 32);
    // Обрезка удаляет тех, у кого меньше usage — значит самые популярные живы
    bool has45 = false;
    for (const auto &it : r.items)
        if (it.text == "m45")
            has45 = true;
    QVERIFY(has45);
}

void TestSyncEngine::testDeletionPropagates()
{
    // Ведомый удалил элемент → мастер с tombstone'ом больше его не возвращает
    SyncEngine::NetworkState local, remote;
    local.items = {mk("keep", 1, 100)};
    remote.tombstones.insert("keep", 500);   // удалено позже появления (100)

    const auto r = SyncEngine::merge(local, remote, true, 32);
    QCOMPARE(r.items.size(), 0);
    QVERIFY(r.tombstones.contains("keep"));
}

void TestSyncEngine::testResurrectAfterDelete()
{
    // Копия появилась ПОСЛЕ удаления → элемент жив
    SyncEngine::NetworkState local, remote;
    remote.tombstones.insert("x", 100);
    local.items = {mk("x", 3, 500)};         // added позже tombstone

    const auto r = SyncEngine::merge(local, remote, true, 32);
    QCOMPARE(r.items.size(), 1);
    QCOMPARE(r.items.first().text, QString("x"));
}

void TestSyncEngine::testOrderingFavoritesThenUsage()
{
    SyncEngine::NetworkState local, remote;
    local.items = {mk("plainHigh", 50, 100), mk("fav", 1, 100, 3),
                   mk("plainLow", 2, 100)};
    const auto r = SyncEngine::merge(local, remote, true, 32);
    QCOMPARE(r.items.size(), 3);
    // Первым — избранный, затем обычные по usage desc
    QCOMPARE(r.items.at(0).text, QString("fav"));
    QCOMPARE(r.items.at(1).text, QString("plainHigh"));
    QCOMPARE(r.items.at(2).text, QString("plainLow"));
}

void TestSyncEngine::testRemoveItemCreatesTombstone()
{
    HistoryManager h;
    h.addToHistory("hello");
    h.addToHistory("world");
    QVERIFY(!h.isDeleted("hello"));
    h.removeItem("hello", 1000);
    QVERIFY(h.isDeleted("hello"));
    QCOMPARE(h.tombstoneAtMs("hello"), qint64(1000));
    for (const auto &it : h.history())
        QVERIFY(it.text != "hello");
}

void TestSyncEngine::testAddToHistoryResurrects()
{
    HistoryManager h;
    h.removeItem("ghost", 1000);
    QVERIFY(h.isDeleted("ghost"));
    h.addToHistory("ghost");
    QVERIFY(!h.isDeleted("ghost"));
}

void TestSyncEngine::testNetworkSettingsRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("settings.yml");

    SettingsManager s;
    s.loadSettings(path);
    s.setNetworkSettings(true, "broker.example.com", 8883, true, "user1",
                         "secretPW", "groupKey", "slave", "room42");

    SettingsManager s2;
    s2.loadSettings(path);
    QCOMPARE(s2.syncEnabled(), true);
    QCOMPARE(s2.brokerHost(), QString("broker.example.com"));
    QCOMPARE(s2.brokerPort(), 8883);
    QCOMPARE(s2.useTls(), true);
    QCOMPARE(s2.brokerUser(), QString("user1"));
    QCOMPARE(s2.brokerPassword(), QString("secretPW"));
    QCOMPARE(s2.syncEncryptionPassword(), QString("groupKey"));
    QCOMPARE(s2.syncRole(), QString("slave"));
    QCOMPARE(s2.isMaster(), false);
    QCOMPARE(s2.syncRoom(), QString("room42"));

    // Пароли не должны лежать в файле открытым текстом
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray content = f.readAll();
    QVERIFY(!content.contains("secretPW"));
    QVERIFY(!content.contains("groupKey"));
}

QTEST_MAIN(TestSyncEngine)
#include "SyncEngineTest.moc"
