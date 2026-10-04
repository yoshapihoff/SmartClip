#include "SettingsDialog.h"
#include "SettingsManager.h"
#include "MqttClient.h"
#include "SyncManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>

SettingsDialog::SettingsDialog(SettingsManager *settingsManager,
                               SyncManager *syncManager, QWidget *parent)
    : QDialog(parent)
    , m_settingsManager(settingsManager)
    , m_syncManager(syncManager)
{
    setupUI();
    loadSettingsToUI();
    updateSyncStatus();

    // Статус соединения может измениться после нажатия «Проверить…».
    if (m_syncManager) {
        connect(m_syncManager, &SyncManager::statusChanged,
                this, &SettingsDialog::updateSyncStatus);
    }

    setWindowTitle("Settings");
    setModal(true);
}

void SettingsDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    QFormLayout *formLayout = new QFormLayout();
    
    // Max Items
    m_maxItemsSpin = new QSpinBox(this);
    m_maxItemsSpin->setMinimum(32);
    m_maxItemsSpin->setMaximum(1000);
    formLayout->addRow("History size", m_maxItemsSpin);
    
    // Launch at startup
    m_launchAtStartupCheck = new QCheckBox(this);
    formLayout->addRow("Launch at startup", m_launchAtStartupCheck);
    
    // Save history on exit
    m_saveHistoryOnExitCheck = new QCheckBox(this);
    formLayout->addRow("Save history on exit", m_saveHistoryOnExitCheck);
    
    mainLayout->addLayout(formLayout);

    // ─────────────────────── Сеть (MQTT-синхронизация) ───────────────────────
    QGroupBox *syncGroup = new QGroupBox("Network sync (MQTT)", this);
    QFormLayout *syncLayout = new QFormLayout(syncGroup);

    m_syncEnabledCheck = new QCheckBox("Enable network sync", syncGroup);
    syncLayout->addRow(m_syncEnabledCheck);

    m_brokerHostEdit = new QLineEdit(syncGroup);
    m_brokerHostEdit->setPlaceholderText("broker.example.com");
    syncLayout->addRow("Broker host", m_brokerHostEdit);

    m_brokerPortSpin = new QSpinBox(syncGroup);
    m_brokerPortSpin->setMinimum(1);
    m_brokerPortSpin->setMaximum(65535);
    m_brokerPortSpin->setValue(1883);
    syncLayout->addRow("Port", m_brokerPortSpin);

    m_useTlsCheck = new QCheckBox("Use TLS", syncGroup);
    syncLayout->addRow(m_useTlsCheck);

    m_brokerUserEdit = new QLineEdit(syncGroup);
    syncLayout->addRow("Login", m_brokerUserEdit);

    m_brokerPasswordEdit = new QLineEdit(syncGroup);
    m_brokerPasswordEdit->setEchoMode(QLineEdit::Password);
    syncLayout->addRow("Password", m_brokerPasswordEdit);

    m_encPasswordEdit = new QLineEdit(syncGroup);
    m_encPasswordEdit->setEchoMode(QLineEdit::Password);
    m_encPasswordEdit->setToolTip(
        "Общий пароль для шифрования пакетов. Одинаковый на всех устройствах.");
    syncLayout->addRow("Encryption password", m_encPasswordEdit);

    m_roleCombo = new QComboBox(syncGroup);
    m_roleCombo->addItem("Master (leading)", QStringLiteral("master"));
    m_roleCombo->addItem("Slave (follower)", QStringLiteral("slave"));
    syncLayout->addRow("Role", m_roleCombo);

    m_roomEdit = new QLineEdit(syncGroup);
    m_roomEdit->setPlaceholderText("smartclip");
    syncLayout->addRow("Room", m_roomEdit);

    // ── Статус соединения с брокером ──
    // Живой индикатор: «Недоступно / Выключено / Подключение… / Подключено /
    // Ошибка». Кнопка «Проверить…» инициирует подключение, не закрывая диалог.
    QHBoxLayout *statusLayout = new QHBoxLayout();
    QLabel *statusCaption = new QLabel("Connection status:", syncGroup);
    m_syncStatusLabel = new QLabel(syncGroup);
    m_syncStatusLabel->setWordWrap(true);
    m_syncStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPushButton *checkButton = new QPushButton("Проверить…", syncGroup);
    checkButton->setToolTip(
        "Подключиться к брокеру прямо сейчас и показать результат.");
    statusLayout->addWidget(statusCaption);
    statusLayout->addWidget(m_syncStatusLabel, 1);
    statusLayout->addWidget(checkButton);
    syncLayout->addRow(statusLayout);

    connect(checkButton, &QPushButton::clicked,
            this, &SettingsDialog::onCheckConnection);

    mainLayout->addWidget(syncGroup);

    connect(m_syncEnabledCheck, &QCheckBox::toggled,
            this, &SettingsDialog::updateSyncFieldsEnabled);

    // Нотация подсказки: не тащим лишнего — просто краткий hint.
    QLabel *hint = new QLabel(
        "Синк опционален. Master задаёт приоритет избранного; у всех устройств "
        "должен быть одинаковый Encryption password.", this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);
    
    // Buttons
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, 
        this
    );

    // Явные английские подписи: иначе Qt переведёт их по локали (ru_RU →
    // «ОК/Отмена») и интерфейс станет смешанным.
    buttonBox->button(QDialogButtonBox::Ok)->setText("OK");
    buttonBox->button(QDialogButtonBox::Cancel)->setText("Cancel");
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::onAccepted);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::onRejected);
    
    mainLayout->addWidget(buttonBox);
}

void SettingsDialog::updateSyncFieldsEnabled()
{
    const bool on = m_syncEnabledCheck->isChecked();
    m_brokerHostEdit->setEnabled(on);
    m_brokerPortSpin->setEnabled(on);
    m_useTlsCheck->setEnabled(on);
    m_brokerUserEdit->setEnabled(on);
    m_brokerPasswordEdit->setEnabled(on);
    m_encPasswordEdit->setEnabled(on);
    m_roleCombo->setEnabled(on);
    m_roomEdit->setEnabled(on);
}

void SettingsDialog::updateSyncStatus()
{
    if (!m_syncStatusLabel)
        return;

    QString text;
    QString color;

    if (m_syncManager) {
        // Живой статус клиента (соединение, ошибки) — берём у SyncManager.
        text = m_syncManager->statusText();
        // Цветовая маркировка (кружок-индикатор).
        if (text.startsWith(QStringLiteral("Подключено")))        color = "#2ec27e";
        else if (text.startsWith(QStringLiteral("Ошибка")))       color = "#e01b24";
        else if (text.startsWith(QStringLiteral("Подключение"))) color = "#f5c211";
        else                                                      color = "#9a9996";
    } else if (!m_settingsManager) {
        text = QStringLiteral("Нет настроек");
        color = "#9a9996";
    } else if (!m_settingsManager->syncEnabled()) {
        text = QStringLiteral("Выключено (синхронизация не включена)");
        color = "#9a9996";
    } else if (!MqttClient::available()) {
        text = QStringLiteral("Недоступно: сборка без модуля Qt6::Mqtt");
        color = "#e01b24";
    } else {
        text = QStringLiteral("Настроено — проверка при открытии/OK");
        color = "#9a9996";
    }

    m_syncStatusLabel->setText(
        QStringLiteral("<span style='color:%1'>&#9679;</span> %2")
            .arg(color, text.toHtmlEscaped()));
}

void SettingsDialog::onCheckConnection()
{
    if (!m_settingsManager) {
        updateSyncStatus();
        return;
    }

    // Сначала переносим ТЕКУЩИЕ значения полей в менеджер (как при OK),
    // чтобы проверялось именно то, что введено сейчас, — и только потом
    // инициируем подключение, не закрывая диалог.
    m_settingsManager->setNetworkSettings(
        m_syncEnabledCheck->isChecked(),
        m_brokerHostEdit->text(),
        m_brokerPortSpin->value(),
        m_useTlsCheck->isChecked(),
        m_brokerUserEdit->text(),
        m_brokerPasswordEdit->text(),
        m_encPasswordEdit->text(),
        m_roleCombo->currentData().toString(),
        m_roomEdit->text());

    if (m_syncManager)
        m_syncManager->checkNow();
    updateSyncStatus();
}

void SettingsDialog::loadSettingsToUI()
{
    if (!m_settingsManager) {
        return;
    }
    
    m_maxItemsSpin->setValue(m_settingsManager->maxItems());
    m_launchAtStartupCheck->setChecked(m_settingsManager->launchAtStartup());
    m_saveHistoryOnExitCheck->setChecked(m_settingsManager->saveHistoryOnExit());

    m_syncEnabledCheck->setChecked(m_settingsManager->syncEnabled());
    m_brokerHostEdit->setText(m_settingsManager->brokerHost());
    m_brokerPortSpin->setValue(m_settingsManager->brokerPort());
    m_useTlsCheck->setChecked(m_settingsManager->useTls());
    m_brokerUserEdit->setText(m_settingsManager->brokerUser());
    m_brokerPasswordEdit->setText(m_settingsManager->brokerPassword());
    m_encPasswordEdit->setText(m_settingsManager->syncEncryptionPassword());
    const int roleIdx = m_roleCombo->findData(m_settingsManager->syncRole());
    m_roleCombo->setCurrentIndex(roleIdx >= 0 ? roleIdx : 0);
    m_roomEdit->setText(m_settingsManager->syncRoom());

    updateSyncFieldsEnabled();
}

void SettingsDialog::onAccepted()
{
    if (!m_settingsManager) {
        return;
    }
    
    // Validate max items value
    int maxItemsValue = m_maxItemsSpin->value();
    if (maxItemsValue < 32 || maxItemsValue > 1000) {
        reject(); // Invalid value, reject dialog
        return;
    }
    
    // Save settings
    m_settingsManager->setMaxItems(maxItemsValue);
    m_settingsManager->setLaunchAtStartup(m_launchAtStartupCheck->isChecked());
    m_settingsManager->setSaveHistoryOnExit(m_saveHistoryOnExitCheck->isChecked());

    m_settingsManager->setNetworkSettings(
        m_syncEnabledCheck->isChecked(),
        m_brokerHostEdit->text(),
        m_brokerPortSpin->value(),
        m_useTlsCheck->isChecked(),
        m_brokerUserEdit->text(),
        m_brokerPasswordEdit->text(),
        m_encPasswordEdit->text(),
        m_roleCombo->currentData().toString(),
        m_roomEdit->text());
    
    accept();
}

void SettingsDialog::onRejected()
{
    reject();
}
