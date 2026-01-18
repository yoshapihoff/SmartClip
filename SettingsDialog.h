#pragma once

#include <QDialog>
#include <QSpinBox>
#include <QCheckBox>

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

    SettingsManager *m_settingsManager;
    
    QSpinBox *m_maxItemsSpin;
    QCheckBox *m_launchAtStartupCheck;
    QCheckBox *m_saveHistoryOnExitCheck;
};
