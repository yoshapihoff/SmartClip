#include "AboutDialog.h"
#include "Version.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>
#include <QIcon>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setWindowTitle(QStringLiteral("About SmartClip"));
    setMinimumWidth(360);
    setModal(true);
}

void AboutDialog::setupUI()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 20, 24, 16);
    lay->setSpacing(8);

    // ── Иконка приложения (та же, что в установленном пакете; в изолированных
    //    тестах ресурс может быть недоступен — тогда картинку не показываем). ──
    QIcon appIcon(QStringLiteral(":/icons/icon.png"));
    if (appIcon.isNull())
        appIcon = QIcon(QStringLiteral(":/icons/tray_black.svg"));
    if (!appIcon.isNull()) {
        auto *iconLabel = new QLabel(this);
        iconLabel->setAlignment(Qt::AlignCenter);
        iconLabel->setPixmap(appIcon.pixmap(96, 96));
        lay->addWidget(iconLabel);
    }

    // ── Название ──
    auto *nameLabel = new QLabel(QStringLiteral("SmartClip"), this);
    nameLabel->setObjectName(QStringLiteral("aboutName"));
    nameLabel->setAlignment(Qt::AlignCenter);
    QFont nameFont = nameLabel->font();
    nameFont.setPointSize(nameFont.pointSize() + 8);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    lay->addWidget(nameLabel);

    // ── Подзаголовок ──
    auto *subtitleLabel =
        new QLabel(QStringLiteral("Clipboard History Manager"), this);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(subtitleLabel);

    lay->addSpacing(6);

    // ── Версия ──
    auto *versionLabel =
        new QLabel(QStringLiteral("Version %1").arg(SMARTCLIP_VERSION_STRING), this);
    versionLabel->setObjectName(QStringLiteral("aboutVersion"));
    versionLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(versionLabel);

    // ── Автор ──
    auto *authorLabel =
        new QLabel(QStringLiteral("Author: Aleksey Zhmikhov"), this);
    authorLabel->setObjectName(QStringLiteral("aboutAuthor"));
    authorLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(authorLabel);

    // ── Лицензия ──
    auto *licenseLabel =
        new QLabel(QStringLiteral("License: MIT"), this);
    licenseLabel->setObjectName(QStringLiteral("aboutLicense"));
    licenseLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(licenseLabel);

    auto *copyrightLabel =
        new QLabel(QStringLiteral("\u00A9 2026 Aleksey Zhmikhov"), this);
    copyrightLabel->setObjectName(QStringLiteral("aboutCopyright"));
    copyrightLabel->setAlignment(Qt::AlignCenter);
    QFont smallFont = copyrightLabel->font();
    smallFont.setPointSize(qMax(1, smallFont.pointSize() - 1));
    copyrightLabel->setFont(smallFont);
    lay->addWidget(copyrightLabel);

    lay->addSpacing(8);

    // ── Кнопка Close ──
    auto *closeButton = new QPushButton(QStringLiteral("Close"), this);
    closeButton->setObjectName(QStringLiteral("aboutClose"));
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    lay->addWidget(closeButton, 0, Qt::AlignCenter);
}
