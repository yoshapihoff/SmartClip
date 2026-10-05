#include <QtTest/QtTest>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryFile>

#include "../HistoryManager.h"

class TestHistoryManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testInitialization();
    void testMaxItems();
    void testDirtyFlag();
    void testAddToHistory();
    void testAddEmptyText();
    void testAddDuplicateText();
    void testTrimToMaxItems();
    void testTrimToMaxItems_prefersToRemoveNonFavorite();
    void testTrimToMaxItems_prefersToRemoveLowerUsageCount();
    void testTrimToMaxItems_removesOldestWhenSamePriority();
    void testClearHistory();
    void testToggleFavorite();
    void testIsFavorite();
    void testIncrementUsageCount();
    void testSortHistory();
    void testLoadSaveHistory();
    void testLoadSaveHistory_maskInMenu();
    void testLoadSaveHistory_favoriteColorIndex();
    void testLoadNonExistentFile();
    void testLoadCorruptedFile();
    void testSetMaskInMenu();
    void testMaskInMenu();
    void testEncryptedRoundTrip();
    void testMigrationFromPlaintext();
    void testCommentPersistence();
    void testChangeTimestamps();

private:
    HistoryManager *m_historyManager;
    QString m_testFilePath;
};

// Крипто: прямой round-trip AES-256-GCM (без файла/хранилища ключей).
#include "../Crypto.h"
void TestHistoryManager::testEncryptedRoundTrip()
{
    if (!Crypto::available())
        QSKIP("OpenSSL недоступен — шифрование не собрано");
    const QByteArray key = Crypto::randomBytes(32);
    QCOMPARE(key.size(), 32);
    const QByteArray plain = QByteArray::fromHex("00ff10deadbeef") + "пароль";
    const QByteArray blob = Crypto::encrypt(plain, key);
    QVERIFY(!blob.isEmpty());
    QVERIFY(blob != plain);
    bool ok = false;
    QCOMPARE(Crypto::decrypt(blob, key, &ok), plain);
    QVERIFY(ok);
    // неверный ключ → провал (ошибка аутентификации GCM)
    bool ok2 = true;
    Crypto::decrypt(blob, Crypto::randomBytes(32), &ok2);
    QVERIFY(!ok2);
}

// Комментарий шифруется и переживает save/load.
void TestHistoryManager::testCommentPersistence()
{
    if (!Crypto::available())
        QSKIP("OpenSSL недоступен");
    const QByteArray key = Crypto::randomBytes(32);
    m_historyManager->setEncryptionKey(key);
    m_historyManager->addToHistory("clip-with-note");
    m_historyManager->setComment("clip-with-note", "важная заметка");
    QCOMPARE(m_historyManager->comment("clip-with-note"), QString("важная заметка"));
    m_historyManager->saveHistory(m_testFilePath);
    // в файле комментарий зашифрован (нет открытого текста)
    {
        QFile f(m_testFilePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        QVERIFY(raw.contains("comment_b64"));
        QVERIFY(!raw.contains("важная заметка"));
    }
    // повторная загрузка тем же ключом — комментарий восстановлен
    HistoryManager hm;
    hm.setEncryptionKey(key);
    hm.loadHistory(m_testFilePath);
    QCOMPARE(hm.comment("clip-with-note"), QString("важная заметка"));
    // чужим ключом — комментарий не расшифрован (пусто)
    HistoryManager hm2;
    hm2.setEncryptionKey(Crypto::randomBytes(32));
    hm2.loadHistory(m_testFilePath);
    QCOMPARE(hm2.comment("clip-with-note"), QString());
}
void TestHistoryManager::testChangeTimestamps()
{
    m_historyManager->addToHistory("clip");
    // Новый элемент: поля никто не менял → метки 0 (легаси-правило ведущего).
    QCOMPARE(m_historyManager->history().first().favChangedAtMs, qint64(0));
    QCOMPARE(m_historyManager->history().first().maskChangedAtMs, qint64(0));
    QCOMPARE(m_historyManager->history().first().commentChangedAtMs, qint64(0));

    m_historyManager->setFavoriteColor("clip", 4);
    m_historyManager->setMaskInMenu("clip", true);
    m_historyManager->setComment("clip", "note");
    QVERIFY(m_historyManager->history().first().favChangedAtMs > 0);
    QVERIFY(m_historyManager->history().first().maskChangedAtMs > 0);
    QVERIFY(m_historyManager->history().first().commentChangedAtMs > 0);

    // Явная метка времени имеет приоритет над «сейчас».
    m_historyManager->setComment("clip", "note2", 123456);
    QCOMPARE(m_historyManager->history().first().commentChangedAtMs, qint64(123456));

    // Метки переживают save/load (иначе LWW теряет знание о правках).
    m_historyManager->saveHistory(m_testFilePath);
    HistoryManager hm;
    hm.loadHistory(m_testFilePath);
    QCOMPARE(hm.history().first().favChangedAtMs,
             m_historyManager->history().first().favChangedAtMs);
    QVERIFY(hm.history().first().maskChangedAtMs > 0);
    QCOMPARE(hm.history().first().commentChangedAtMs, qint64(123456));
}

void TestHistoryManager::testMigrationFromPlaintext()
{
    if (!Crypto::available())
        QSKIP("OpenSSL недоступен");
    // 1) пишем ОТКРЫТЫЙ файл (старый формат)
    m_historyManager->addToHistory("secret-pass");
    m_historyManager->saveHistory(m_testFilePath);
    {
        QFile f(m_testFilePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        QVERIFY(raw.contains("text_b64"));
        QVERIFY(!raw.contains("v2:"));   // ещё открыто
    }
    // 2) с ключом: загрузка помечает файл как подлежащий миграции
    const QByteArray key = Crypto::randomBytes(32);
    HistoryManager hm2;
    hm2.setEncryptionKey(key);
    QVERIFY(hm2.loadHistory(m_testFilePath));          // needMigrate == true
    QCOMPARE(hm2.history().size(), 1);
    QCOMPARE(hm2.history().first().text, QString("secret-pass"));
    // 3) сохраняем → файл зашифрован (v2:), открытого текста нет
    hm2.saveHistory(m_testFilePath);
    {
        QFile f(m_testFilePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray raw = f.readAll();
        QVERIFY(raw.contains("v2:"));
        QVERIFY(!raw.contains("secret-pass"));
    }
    // 4) загрузка тем же ключом — текст восстанавливается, миграция не нужна
    HistoryManager hm3;
    hm3.setEncryptionKey(key);
    QVERIFY(!hm3.loadHistory(m_testFilePath));         // уже шифртекст
    QCOMPARE(hm3.history().size(), 1);
    QCOMPARE(hm3.history().first().text, QString("secret-pass"));
    // 5) чужим ключом — расшифровка невалидна (GCM), текст не отдаётся
    HistoryManager hm4;
    hm4.setEncryptionKey(Crypto::randomBytes(32));
    hm4.loadHistory(m_testFilePath);
    for (const auto &it : hm4.history())
        QVERIFY(it.text.isEmpty());
}

void TestHistoryManager::init()
{
    m_historyManager = new HistoryManager();
    m_testFilePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/test_history.txt";
    
    // Clean up any existing test file
    QFile::remove(m_testFilePath);
}

void TestHistoryManager::cleanup()
{
    QFile::remove(m_testFilePath);
    delete m_historyManager;
}

void TestHistoryManager::testInitialization()
{
    QVERIFY(m_historyManager != nullptr);
    QCOMPARE(m_historyManager->history().size(), 0);
    QCOMPARE(m_historyManager->maxItems(), 32);
    QCOMPARE(m_historyManager->isDirty(), false);
}

void TestHistoryManager::testMaxItems()
{
    QCOMPARE(m_historyManager->maxItems(), 32);
    
    m_historyManager->setMaxItems(10);
    QCOMPARE(m_historyManager->maxItems(), 10);
    QCOMPARE(m_historyManager->isDirty(), true);
    
    // Setting same value does not clear dirty (only loadHistory clears it)
    m_historyManager->setMaxItems(10);
    QCOMPARE(m_historyManager->isDirty(), true);
}

void TestHistoryManager::testDirtyFlag()
{
    QCOMPARE(m_historyManager->isDirty(), false);
    
    m_historyManager->addToHistory("test");
    QCOMPARE(m_historyManager->isDirty(), true);
}

void TestHistoryManager::testAddToHistory()
{
    m_historyManager->addToHistory("test item");
    QCOMPARE(m_historyManager->history().size(), 1);
    QCOMPARE(m_historyManager->history()[0].text, "test item");
    QCOMPARE(m_historyManager->history()[0].usageCount, 0);
    QVERIFY(m_historyManager->history()[0].addedAtMs > 0);
    QCOMPARE(m_historyManager->history()[0].favoriteColorIndex, -1);
    QCOMPARE(m_historyManager->isDirty(), true);
}

void TestHistoryManager::testAddEmptyText()
{
    m_historyManager->addToHistory("");
    QCOMPARE(m_historyManager->history().size(), 0);
    
    m_historyManager->addToHistory("   ");
    QCOMPARE(m_historyManager->history().size(), 0);
    
    m_historyManager->addToHistory("\t\n");
    QCOMPARE(m_historyManager->history().size(), 0);
}

void TestHistoryManager::testAddDuplicateText()
{
    m_historyManager->addToHistory("duplicate");
    QCOMPARE(m_historyManager->history().size(), 1);
    qint64 firstTime = m_historyManager->history()[0].addedAtMs;
    
    // Wait a bit to ensure different timestamp
    QTest::qWait(10);
    
    m_historyManager->addToHistory("duplicate");
    QCOMPARE(m_historyManager->history().size(), 1);
    QVERIFY(m_historyManager->history()[0].addedAtMs > firstTime);
}

void TestHistoryManager::testTrimToMaxItems()
{
    m_historyManager->setMaxItems(3);
    
    m_historyManager->addToHistory("item1");
    m_historyManager->addToHistory("item2");
    m_historyManager->addToHistory("item3");
    QCOMPARE(m_historyManager->history().size(), 3);
    
    m_historyManager->addToHistory("item4");
    QCOMPARE(m_historyManager->history().size(), 3);
    
    // При равных isFavorite и usageCount удаляется самый старый по времени добавления
    bool found = false;
    for (const auto &item : m_historyManager->history()) {
        if (item.text == "item1") {
            found = true;
            break;
        }
    }
    QCOMPARE(found, false);
}

void TestHistoryManager::testTrimToMaxItems_prefersToRemoveNonFavorite()
{
    m_historyManager->setMaxItems(4);
    m_historyManager->addToHistory("item1");
    m_historyManager->addToHistory("item2");
    m_historyManager->addToHistory("item3");
    m_historyManager->addToHistory("item4");
    m_historyManager->toggleFavorite("item2");
    QCOMPARE(m_historyManager->history().size(), 4);
    
    m_historyManager->setMaxItems(3);
    QCOMPARE(m_historyManager->history().size(), 3);
    
    // Избранный item2 должен остаться; удалён один из неизбранных (первый по приоритету — старый с малым usageCount)
    QVERIFY(m_historyManager->isFavorite("item2"));
    bool item2Found = false;
    for (const auto &item : m_historyManager->history()) {
        if (item.text == "item2") item2Found = true;
    }
    QVERIFY(item2Found);
}

void TestHistoryManager::testTrimToMaxItems_prefersToRemoveLowerUsageCount()
{
    m_historyManager->setMaxItems(4);
    m_historyManager->addToHistory("item1");
    m_historyManager->addToHistory("item2");
    m_historyManager->addToHistory("item3");
    m_historyManager->addToHistory("item4");
    m_historyManager->incrementUsageCount("item2");
    m_historyManager->incrementUsageCount("item3");
    QCOMPARE(m_historyManager->history().size(), 4);
    
    m_historyManager->setMaxItems(3);
    QCOMPARE(m_historyManager->history().size(), 3);
    
    // Удалён элемент с меньшим usageCount (item1 или item4); item2 и item3 с usageCount=1 должны остаться
    bool item1Found = false;
    for (const auto &item : m_historyManager->history()) {
        if (item.text == "item1") item1Found = true;
    }
    QCOMPARE(item1Found, false); // item1 добавлен первым (старейший при равном usageCount=0 с item4)
}

void TestHistoryManager::testTrimToMaxItems_removesOldestWhenSamePriority()
{
    m_historyManager->setMaxItems(3);
    m_historyManager->addToHistory("old");
    QTest::qWait(5);
    m_historyManager->addToHistory("mid");
    QTest::qWait(5);
    m_historyManager->addToHistory("new");
    QCOMPARE(m_historyManager->history().size(), 3);
    
    m_historyManager->addToHistory("newest");
    QCOMPARE(m_historyManager->history().size(), 3);
    
    // При равных isFavorite и usageCount удаляется самый старый — "old"
    bool oldFound = false;
    for (const auto &item : m_historyManager->history()) {
        if (item.text == "old") oldFound = true;
    }
    QCOMPARE(oldFound, false);
}

void TestHistoryManager::testClearHistory()
{
    m_historyManager->addToHistory("item1");
    m_historyManager->addToHistory("item2");
    QCOMPARE(m_historyManager->history().size(), 2);
    
    m_historyManager->clearHistory();
    QCOMPARE(m_historyManager->history().size(), 0);
    QCOMPARE(m_historyManager->isDirty(), true);
}

void TestHistoryManager::testToggleFavorite()
{
    m_historyManager->addToHistory("favorite item");
    QCOMPARE(m_historyManager->isFavorite("favorite item"), false);
    
    m_historyManager->toggleFavorite("favorite item");
    QCOMPARE(m_historyManager->isFavorite("favorite item"), true);
    QCOMPARE(m_historyManager->isDirty(), true);
    
    m_historyManager->toggleFavorite("favorite item");
    QCOMPARE(m_historyManager->isFavorite("favorite item"), false);
    
    // Test toggling non-existent item
    m_historyManager->toggleFavorite("non-existent");
    // Should not crash
}

void TestHistoryManager::testIsFavorite()
{
    m_historyManager->addToHistory("item1");
    m_historyManager->addToHistory("item2");
    
    QCOMPARE(m_historyManager->isFavorite("item1"), false);
    QCOMPARE(m_historyManager->isFavorite("item2"), false);
    QCOMPARE(m_historyManager->isFavorite("non-existent"), false);
    
    m_historyManager->toggleFavorite("item1");
    QCOMPARE(m_historyManager->isFavorite("item1"), true);
    QCOMPARE(m_historyManager->isFavorite("item2"), false);
}

void TestHistoryManager::testIncrementUsageCount()
{
    m_historyManager->addToHistory("popular item");
    QCOMPARE(m_historyManager->history()[0].usageCount, 0);
    
    m_historyManager->incrementUsageCount("popular item");
    QCOMPARE(m_historyManager->history()[0].usageCount, 1);
    QCOMPARE(m_historyManager->isDirty(), true);
    
    m_historyManager->incrementUsageCount("popular item");
    QCOMPARE(m_historyManager->history()[0].usageCount, 2);
    
    // Test incrementing non-existent item
    m_historyManager->incrementUsageCount("non-existent");
    // Should not crash
}

void TestHistoryManager::testSortHistory()
{
    // Add items in specific order
    m_historyManager->addToHistory("normal item");
    QTest::qWait(10);
    
    m_historyManager->addToHistory("favorite item");
    m_historyManager->toggleFavorite("favorite item");
    QTest::qWait(10);
    
    m_historyManager->addToHistory("popular item");
    m_historyManager->incrementUsageCount("popular item");
    m_historyManager->incrementUsageCount("popular item");
    
    // Sort should be: favorite, popular, normal
    const auto &history = m_historyManager->history();
    QCOMPARE(history.size(), 3);
    QCOMPARE(history[0].text, "favorite item"); // Favorite first
    QCOMPARE(history[1].text, "popular item"); // High usage count
    QCOMPARE(history[2].text, "normal item");   // Normal
    
    // Test that recent usage affects sorting
    QTest::qWait(10);
    m_historyManager->incrementUsageCount("normal item");
    
    // Now normal item should move up due to recent usage
    // But popular item still has higher usage count (2 vs 1)
    const auto &updatedHistory = m_historyManager->history();
    QCOMPARE(updatedHistory[0].text, "favorite item"); // Still favorite first
    QCOMPARE(updatedHistory[1].text, "popular item"); // Still higher usage count
    QCOMPARE(updatedHistory[2].text, "normal item");   // Lower usage count despite recent time
}

void TestHistoryManager::testLoadSaveHistory()
{
    // Create test data
    m_historyManager->addToHistory("item1");
    m_historyManager->addToHistory("item2");
    m_historyManager->toggleFavorite("item1");
    m_historyManager->incrementUsageCount("item2");
    
    // Save history
    m_historyManager->saveHistory(m_testFilePath);
    QVERIFY(QFile::exists(m_testFilePath));
    
    // Clear and reload
    // clearHistory() теперь удаляет только неизбранные элементы, избранные остаются
    m_historyManager->clearHistory();
    QCOMPARE(m_historyManager->history().size(), 1); // item1 (избранное) остался, item2 удалён
    
    m_historyManager->loadHistory(m_testFilePath);
    QCOMPARE(m_historyManager->history().size(), 2);
    
    // Verify data
    QCOMPARE(m_historyManager->isFavorite("item1"), true);
    QCOMPARE(m_historyManager->isFavorite("item2"), false);
    
    bool found1 = false, found2 = false;
    for (const auto &item : m_historyManager->history()) {
        if (item.text == "item1") {
            QCOMPARE(item.usageCount, 0);
            found1 = true;
        } else if (item.text == "item2") {
            QCOMPARE(item.usageCount, 1);
            found2 = true;
        }
    }
    QVERIFY(found1 && found2);
    QCOMPARE(m_historyManager->isDirty(), false);
}

void TestHistoryManager::testLoadSaveHistory_maskInMenu()
{
    m_historyManager->addToHistory("item1");
    m_historyManager->addToHistory("item2");
    m_historyManager->setMaskInMenu("item1", true);
    m_historyManager->setMaskInMenu("item2", false);

    m_historyManager->saveHistory(m_testFilePath);
    QVERIFY(QFile::exists(m_testFilePath));

    m_historyManager->clearHistory();
    m_historyManager->loadHistory(m_testFilePath);

    QCOMPARE(m_historyManager->history().size(), 2);
    QCOMPARE(m_historyManager->maskInMenu("item1"), true);
    QCOMPARE(m_historyManager->maskInMenu("item2"), false);

    bool found1 = false, found2 = false;
    for (const auto &item : m_historyManager->history()) {
        if (item.text == "item1") {
            QCOMPARE(item.maskInMenu, true);
            found1 = true;
        } else if (item.text == "item2") {
            QCOMPARE(item.maskInMenu, false);
            found2 = true;
        }
    }
    QVERIFY(found1 && found2);
}

void TestHistoryManager::testLoadSaveHistory_favoriteColorIndex()
{
    m_historyManager->addToHistory("fav_red");
    m_historyManager->addToHistory("fav_green");
    m_historyManager->addToHistory("normal");
    m_historyManager->toggleFavorite("fav_red");
    m_historyManager->toggleFavorite("fav_green");
    m_historyManager->setFavoriteColor("fav_red", 0);
    m_historyManager->setFavoriteColor("fav_green", 3);

    m_historyManager->saveHistory(m_testFilePath);
    QVERIFY(QFile::exists(m_testFilePath));

    m_historyManager->clearHistory();
    m_historyManager->loadHistory(m_testFilePath);

    QCOMPARE(m_historyManager->history().size(), 3);
    QCOMPARE(m_historyManager->favoriteColorIndex("fav_red"), 0);
    QCOMPARE(m_historyManager->favoriteColorIndex("fav_green"), 3);
    QCOMPARE(m_historyManager->favoriteColorIndex("normal"), -1);

    for (const auto &item : m_historyManager->history()) {
        if (item.text == "fav_red")
            QCOMPARE(item.favoriteColorIndex, 0);
        else if (item.text == "fav_green")
            QCOMPARE(item.favoriteColorIndex, 3);
        else if (item.text == "normal")
            QCOMPARE(item.favoriteColorIndex, -1);
    }
}

void TestHistoryManager::testSetMaskInMenu()
{
    m_historyManager->addToHistory("password");
    QCOMPARE(m_historyManager->history()[0].maskInMenu, false);

    m_historyManager->setMaskInMenu("password", true);
    QCOMPARE(m_historyManager->history()[0].maskInMenu, true);
    QCOMPARE(m_historyManager->isDirty(), true);

    m_historyManager->setMaskInMenu("password", false);
    QCOMPARE(m_historyManager->history()[0].maskInMenu, false);

    // Non-existent item: no crash
    m_historyManager->setMaskInMenu("non-existent", true);
    QCOMPARE(m_historyManager->history().size(), 1);
}

void TestHistoryManager::testMaskInMenu()
{
    m_historyManager->addToHistory("item1");
    m_historyManager->addToHistory("item2");

    QCOMPARE(m_historyManager->maskInMenu("item1"), false);
    QCOMPARE(m_historyManager->maskInMenu("item2"), false);
    QCOMPARE(m_historyManager->maskInMenu("non-existent"), false);

    m_historyManager->setMaskInMenu("item1", true);
    QCOMPARE(m_historyManager->maskInMenu("item1"), true);
    QCOMPARE(m_historyManager->maskInMenu("item2"), false);
}

void TestHistoryManager::testLoadNonExistentFile()
{
    m_historyManager->loadHistory("/path/that/does/not/exist.txt");
    QCOMPARE(m_historyManager->history().size(), 0);
}

void TestHistoryManager::testLoadCorruptedFile()
{
    // Create corrupted file
    QFile file(m_testFilePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << "corrupted data\n";
    out << "invalid format\n";
    file.close();
    
    m_historyManager->loadHistory(m_testFilePath);
    QCOMPARE(m_historyManager->history().size(), 0);
}

QTEST_MAIN(TestHistoryManager)
#include "HistoryManagerTest.moc"
