#pragma once

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>

class SettingsManager;
class SyncManager;
class QLabel;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // syncManager опционален: без него строка статуса показывает только
    // состояние по настройкам (для тестов и обратной совместимости).
    explicit SettingsDialog(SettingsManager *settingsManager,
                            SyncManager *syncManager = nullptr,
                            QWidget *parent = nullptr);
    ~SettingsDialog() = default;

private slots:
    void onAccepted();
    void onRejected();
    void updateSyncStatus();     // обновить строку статуса соединения
    void onCheckConnection();    // кнопка «Проверить соединение»

private:
    void setupUI();
    void loadSettingsToUI();
    void updateSyncFieldsEnabled();

    SettingsManager *m_settingsManager;
    SyncManager *m_syncManager = nullptr;
    class QLabel *m_syncStatusLabel = nullptr;

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

