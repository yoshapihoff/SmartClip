#pragma once

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>

class SettingsManager;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(SettingsManager *settingsManager, QWidget *parent = nullptr);
    ~SettingsDialog() = default;

private slots:
    void onAccepted();
    void onRejected();

private:
    void setupUI();
    void loadSettingsToUI();
    void updateSyncFieldsEnabled();

    SettingsManager *m_settingsManager;

    QSpinBox *m_maxItemsSpin;
    QCheckBox *m_launchAtStartupCheck;
    QCheckBox *m_saveHistoryOnExitCheck;

    // Сеть (MQTT)
    QCheckBox *m_syncEnabledCheck;
    QLineEdit *m_brokerHostEdit;
    QSpinBox *m_brokerPortSpin;
    QCheckBox *m_useTlsCheck;
    QLineEdit *m_brokerUserEdit;
    QLineEdit *m_brokerPasswordEdit;
    QLineEdit *m_encPasswordEdit;
    QComboBox *m_roleCombo;
    QLineEdit *m_roomEdit;
};
