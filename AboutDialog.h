#pragma once

#include <QDialog>

/**
 * AboutDialog — небольшое окно «О программе».
 *
 * Показывает название, версию (из файла VERSION через Version.h), автора и
 * лицензию. Открывается из меню трея («About») и из футера попапа.
 * Интерфейс — единый английский (как и всё приложение).
 */
class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() = default;

private:
    void setupUI();
};
