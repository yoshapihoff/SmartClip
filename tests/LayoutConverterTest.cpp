#include <QtTest/QtTest>
#include "LayoutConverter.h"

/**
 * Тесты смены раскладки текста.
 *
 * Часть проверок зависит от системных раскладок: если в системе меньше двух
 * раскладок (или карты клавиш недоступны), такие тесты пропускаются через
 * QSKIP, чтобы набор был зелёным на любой машине.
 */
class LayoutConverterTest final : public QObject
{
    Q_OBJECT

private slots:
    void availableLayoutsNotEmpty()
    {
        const QStringList layouts = LayoutConverter::availableLayouts();
        QVERIFY(!layouts.isEmpty());
        // Кэш: повторный вызов возвращает тот же список.
        QCOMPARE(LayoutConverter::availableLayouts(), layouts);
    }

    void convertUsToRu()
    {
        if (!LayoutConverter::available())
            QSKIP("в системе меньше двух раскладок");
        // «Ghbdtn» — как если бы «Привет» набрали в английской раскладке.
        const QString out = LayoutConverter::convert(
            QStringLiteral("Ghbdtn"), QStringLiteral("us"), QStringLiteral("ru"));
        QCOMPARE(out, QString::fromUtf8("Привет"));
    }

    void roundTrip()
    {
        if (!LayoutConverter::available())
            QSKIP("в системе меньше двух раскладок");
        // Строка чисто в русской раскладке: ru→us→ru обязана вернуться к исходной.
        const QString src = QString::fromUtf8("Привет");
        const QString ascii = LayoutConverter::convert(src, QStringLiteral("ru"),
                                                       QStringLiteral("us"));
        QCOMPARE(ascii, QStringLiteral("Ghbdtn"));      // что было бы набрано в us
        const QString back = LayoutConverter::convert(ascii, QStringLiteral("us"),
                                                      QStringLiteral("ru"));
        QCOMPARE(back, src);
    }

    void unchangedTextReturnsEmpty()
    {
        // Строка без букв исходной раскладки (цифры/пробелы) — «нечего менять»,
        // функция сигналит пустой строкой, чтобы не трогать буфер обмена.
        const QString out = LayoutConverter::convert(
            QStringLiteral("  123  "), QStringLiteral("us"), QStringLiteral("ru"));
        QVERIFY(out.isEmpty());
    }

    void sameLayoutReturnsEmpty()
    {
        const QString out = LayoutConverter::convert(
            QStringLiteral("hello"), QStringLiteral("us"), QStringLiteral("us"));
        QVERIFY(out.isEmpty());
    }

    void unknownLayoutsReturnEmpty()
    {
        const QString out = LayoutConverter::convert(
            QStringLiteral("Ghbdtn"), QStringLiteral("zz"), QStringLiteral("qq"));
        QVERIFY(out.isEmpty());
    }

    void guessLayoutByText()
    {
        if (!LayoutConverter::available())
            QSKIP("в системе меньше двух раскладок");
        // Латиница → это us, кириллица → ru (при наличии обеих раскладок).
        const QStringList layouts = LayoutConverter::availableLayouts();
        if (layouts.contains(QStringLiteral("us"))
            && layouts.contains(QStringLiteral("ru"))) {
            QCOMPARE(LayoutConverter::guessLayout(QStringLiteral("hello")),
                     QStringLiteral("us"));
            QCOMPARE(LayoutConverter::guessLayout(QString::fromUtf8("привет")),
                     QStringLiteral("ru"));
        }
        // Символ, которого нет ни в одной раскладке (эмодзи) — определить нельзя.
        QVERIFY(LayoutConverter::guessLayout(QString::fromUtf8("\xF0\x9F\x98\x80"))
                    .isEmpty());
    }

    void mixedTextKeepsTargetSubstring()
    {
        if (!LayoutConverter::available())
            QSKIP("в системе меньше двух раскладок");
        const QStringList layouts = LayoutConverter::availableLayouts();
        if (!layouts.contains(QStringLiteral("us"))
            || !layouts.contains(QStringLiteral("ru")))
            QSKIP("нет пары us/ru");
        // Подстрока, уже набранная в целевую раскладку (в середине/конце),
        // остаётся как была.
        QCOMPARE(LayoutConverter::convert(
                     QStringLiteral("Ghbdtn ") + QString::fromUtf8("привет"),
                     QStringLiteral("us"), QStringLiteral("ru")),
                 QString::fromUtf8("Привет привет"));
        QCOMPARE(LayoutConverter::convert(
                     QString::fromUtf8("привет ") + QStringLiteral("Ghbdtn"),
                     QStringLiteral("us"), QStringLiteral("ru")),
                 QString::fromUtf8("привет Привет"));
    }

    void sharedPunctuationPreserved()
    {
        if (!LayoutConverter::available())
            QSKIP("в системе меньше двух раскладок");
        const QStringList layouts = LayoutConverter::availableLayouts();
        if (!layouts.contains(QStringLiteral("us"))
            || !layouts.contains(QStringLiteral("ru")))
            QSKIP("нет пары us/ru");
        // Символы, общие для обеих раскладок, не подменяются.
        QCOMPARE(LayoutConverter::convert(
                     QStringLiteral("Ghbdtn,"),
                     QStringLiteral("us"), QStringLiteral("ru")),
                 QString::fromUtf8("Привет,"));
        QCOMPARE(LayoutConverter::convert(
                     QString::fromUtf8("привет,"),
                     QStringLiteral("ru"), QStringLiteral("us")),
                 QStringLiteral("ghbdtn,"));
    }

    void nextLayoutCycles()
    {
        const QStringList layouts = LayoutConverter::availableLayouts();
        if (layouts.size() < 2)
            QSKIP("в системе меньше двух раскладок");
        const QString first = layouts.first();
        const QString second = layouts.at(1);
        QCOMPARE(LayoutConverter::nextLayout(first), second);
        // Полный цикл возвращает к началу.
        QString cur = first;
        for (int i = 0; i < layouts.size(); ++i)
            cur = LayoutConverter::nextLayout(cur);
        QCOMPARE(cur, first);
        // Неизвестная раскладка → начинаем с первой.
        QCOMPARE(LayoutConverter::nextLayout(QStringLiteral("zzz")), first);
    }
};

QTEST_GUILESS_MAIN(LayoutConverterTest)
#include "LayoutConverterTest.moc"
