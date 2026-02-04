#include "SettingsDialog.h"
#include "SettingsManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>

SettingsDialog::SettingsDialog(SettingsManager *settingsManager, QWidget *parent)
    : QDialog(parent)
    , m_settingsManager(settingsManager)
{
    setupUI();
    loadSettingsToUI();
    
    setWindowTitle("Settings");
    setModal(true);
}

void SettingsDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    QFormLayout *formLayout = new QFormLayout();
    
    // Max Items
    m_maxItemsSpin = new QSpinBox(this);
    m_maxItemsSpin->setMinimum(1);
    m_maxItemsSpin->setMaximum(1000);
    formLayout->addRow("History size", m_maxItemsSpin);
    
    // Launch at startup
    m_launchAtStartupCheck = new QCheckBox(this);
    formLayout->addRow("Launch at startup", m_launchAtStartupCheck);
    
    // Save history on exit
    m_saveHistoryOnExitCheck = new QCheckBox(this);
    formLayout->addRow("Save history on exit", m_saveHistoryOnExitCheck);
    
    mainLayout->addLayout(formLayout);
    
    // Buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, 
        this
    );
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::onRejected);
    
    mainLayout->addWidget(buttonBox);
}

void SettingsDialog::loadSettingsToUI()
{
    if (!m_settingsManager) {
        return;
    }
    
    m_maxItemsSpin->setValue(m_settingsManager->maxItems());
    m_launchAtStartupCheck->setChecked(m_settingsManager->launchAtStartup());
    m_saveHistoryOnExitCheck->setChecked(m_settingsManager->saveHistoryOnExit());
}

void SettingsDialog::onAccepted()
{
    if (!m_settingsManager) {
        return;
    }
    
    // Validate max items value
    int maxItemsValue = m_maxItemsSpin->value();
    if (maxItemsValue < 1 || maxItemsValue > 1000) {
        reject(); // Invalid value, reject dialog
        return;
    }
    
    // Save settings
    m_settingsManager->setMaxItems(maxItemsValue);
    m_settingsManager->setLaunchAtStartup(m_launchAtStartupCheck->isChecked());
    m_settingsManager->setSaveHistoryOnExit(m_saveHistoryOnExitCheck->isChecked());
    
    accept();
}

void SettingsDialog::onRejected()
{
    reject();
}
