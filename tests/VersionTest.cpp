#include "Version.h"

#include <QtTest>
#include <QRegularExpression>

// Проверяет, что версия пробрасывается в код из файла VERSION (через CMake)
// и имеет форму MAJOR.MINOR.PATCH.
class VersionTest : public QObject
{
    Q_OBJECT

private slots:
    void versionIsDefined()
    {
        const QString v = QString::fromLatin1(SMARTCLIP_VERSION_STRING);
        QVERIFY2(!v.isEmpty(), "SMARTCLIP_VERSION_STRING пуст");
        QVERIFY2(v != QStringLiteral("dev"),
                 "версия не проброшена (SMARTCLIP_VERSION не задан при сборке)");
    }

    void versionHasSemverShape()
    {
        const QString v = QString::fromLatin1(SMARTCLIP_VERSION_STRING);
        const QRegularExpression re(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));
        QVERIFY2(re.match(v).hasMatch(),
                 qPrintable(QStringLiteral("версия не в форме MAJOR.MINOR.PATCH: %1").arg(v)));
    }
};

QTEST_MAIN(VersionTest)
#include "VersionTest.moc"
