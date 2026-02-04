#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QSpinBox>
#include <QCheckBox>
#include <QDialogButtonBox>

#include "../SettingsDialog.h"
#include "../SettingsManager.h"

class TestSettingsDialog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testInitialization();
    void testLoadSettingsToUI();
    void testLoadSettingsWithNullManager();
    void testAcceptWithValidSettings();
    void testAcceptWithInvalidMaxItems();
    void testAcceptWithNullManager();
    void testRejectDialog();
    void testUISetup();

private:
    SettingsManager *m_settingsManager;
    SettingsDialog *m_dialog;
};

void TestSettingsDialog::init()
{
    m_settingsManager = new SettingsManager();
    m_dialog = new SettingsDialog(m_settingsManager);
}

void TestSettingsDialog::cleanup()
{
    delete m_dialog;
    delete m_settingsManager;
}

void TestSettingsDialog::testInitialization()
{
    QVERIFY(m_dialog != nullptr);
    QCOMPARE(m_dialog->isModal(), true);
    QCOMPARE(m_dialog->windowTitle(), "Settings");
    QVERIFY(m_dialog->findChild<QSpinBox*>() != nullptr);
    
    QList<QCheckBox*> checkBoxes = m_dialog->findChildren<QCheckBox*>();
    QVERIFY(checkBoxes.size() >= 2);
}

void TestSettingsDialog::testLoadSettingsToUI()
{
    m_settingsManager->setMaxItems(50);
    m_settingsManager->setLaunchAtStartup(true);
    m_settingsManager->setSaveHistoryOnExit(false);

    SettingsDialog *newDialog = new SettingsDialog(m_settingsManager);
    
    QSpinBox *maxItemsSpin = newDialog->findChild<QSpinBox*>();
    QVERIFY(maxItemsSpin != nullptr);
    QCOMPARE(maxItemsSpin->value(), 50);

    QList<QCheckBox*> checkBoxes = newDialog->findChildren<QCheckBox*>();
    QVERIFY(checkBoxes.size() >= 2);
    
    delete newDialog;
}

void TestSettingsDialog::testLoadSettingsWithNullManager()
{
    SettingsDialog *nullDialog = new SettingsDialog(nullptr);
    
    QSpinBox *maxItemsSpin = nullDialog->findChild<QSpinBox*>();
    QVERIFY(maxItemsSpin != nullptr);
    
    delete nullDialog;
}

void TestSettingsDialog::testAcceptWithValidSettings()
{
    QSpinBox *maxItemsSpin = m_dialog->findChild<QSpinBox*>();
    QVERIFY(maxItemsSpin != nullptr);
    maxItemsSpin->setValue(100);

    QList<QCheckBox*> checkBoxes = m_dialog->findChildren<QCheckBox*>();
    QVERIFY(checkBoxes.size() >= 2);
    
    checkBoxes[0]->setChecked(true);
    checkBoxes[1]->setChecked(false);

    QDialogButtonBox *buttonBox = m_dialog->findChild<QDialogButtonBox*>();
    QVERIFY(buttonBox != nullptr);
    
    QSignalSpy spy(m_settingsManager, &SettingsManager::settingsChanged);
    
    buttonBox->accepted();
    
    QCOMPARE(m_settingsManager->maxItems(), 100);
    QCOMPARE(m_settingsManager->launchAtStartup(), true);
    QCOMPARE(m_settingsManager->saveHistoryOnExit(), false);
    QCOMPARE(spy.count(), 3);
}

void TestSettingsDialog::testAcceptWithInvalidMaxItems()
{
    QSpinBox *maxItemsSpin = m_dialog->findChild<QSpinBox*>();
    QVERIFY(maxItemsSpin != nullptr);
    maxItemsSpin->setMaximum(2000);
    maxItemsSpin->setValue(1500);

    QDialogButtonBox *buttonBox = m_dialog->findChild<QDialogButtonBox*>();
    QVERIFY(buttonBox != nullptr);
    
    int originalMaxItems = m_settingsManager->maxItems();
    
    buttonBox->accepted();
    
    QCOMPARE(m_settingsManager->maxItems(), originalMaxItems);
}

void TestSettingsDialog::testAcceptWithNullManager()
{
    SettingsDialog *nullDialog = new SettingsDialog(nullptr);
    
    QDialogButtonBox *buttonBox = nullDialog->findChild<QDialogButtonBox*>();
    QVERIFY(buttonBox != nullptr);
    
    buttonBox->accepted();
    
    delete nullDialog;
}

void TestSettingsDialog::testRejectDialog()
{
    QDialogButtonBox *buttonBox = m_dialog->findChild<QDialogButtonBox*>();
    QVERIFY(buttonBox != nullptr);
    
    QSignalSpy spy(m_dialog, &QDialog::rejected);
    
    buttonBox->rejected();
    
    QCOMPARE(spy.count(), 1);
}

void TestSettingsDialog::testUISetup()
{
    QSpinBox *maxItemsSpin = m_dialog->findChild<QSpinBox*>();
    QVERIFY(maxItemsSpin != nullptr);
    QCOMPARE(maxItemsSpin->minimum(), 1);
    QCOMPARE(maxItemsSpin->maximum(), 1000);

    QList<QCheckBox*> checkBoxes = m_dialog->findChildren<QCheckBox*>();
    QVERIFY(checkBoxes.size() >= 2);

    QDialogButtonBox *buttonBox = m_dialog->findChild<QDialogButtonBox*>();
    QVERIFY(buttonBox != nullptr);
    QVERIFY(buttonBox->standardButtons() & QDialogButtonBox::Ok);
    QVERIFY(buttonBox->standardButtons() & QDialogButtonBox::Cancel);
}

QTEST_MAIN(TestSettingsDialog)
#include "SettingsDialogTest.moc"
