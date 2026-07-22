#include "HelpDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setWindowTitle("Help — SmartClip");
    setMinimumWidth(480);
    setModal(true);
}

void HelpDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // --- Заголовок ---
    QLabel *titleLabel = new QLabel("SmartClip — quick reference", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    // --- 1. Избранное ---
    QLabel *favTitle = new QLabel("<b>★ How to add an item to Favorites</b>", this);
    mainLayout->addWidget(favTitle);

    QLabel *favText = new QLabel(
#if defined(Q_OS_MAC)
        "Hold <b>Ctrl</b> (⌃ on macOS) and <b>left-click</b> on any item in the tray menu. "
#else
        "Hold <b>Ctrl</b> and <b>left-click</b> on any item in the tray menu. "
#endif
        "A colored dot will appear next to it, marking it as a favorite. "
        "Favorites stay pinned in the history so you can always find them quickly. "
        "To remove from favorites, Ctrl+click the same item again.",
        this
    );
    favText->setWordWrap(true);
    mainLayout->addWidget(favText);

    // --- 2. Скрыть пароль ---
    QLabel *maskTitle = new QLabel("<b>🔒 How to hide a password / sensitive text</b>", this);
    mainLayout->addWidget(maskTitle);

    QLabel *maskText = new QLabel(
#if defined(Q_OS_MAC)
        "Hold <b>Shift</b> (⇧) and <b>left-click</b> on an item in the tray menu. "
#else
        "Hold <b>Shift</b> and <b>left-click</b> on an item in the tray menu. "
#endif
        "The text will be masked — only the first and last few characters will be visible, "
        "and the rest will be replaced with asterisks (***). "
        "Shift+click again to show the full text.",
        this
    );
    maskText->setWordWrap(true);
    mainLayout->addWidget(maskText);

    // --- 3. Рейтинг / наполнение буфера ---
    QLabel *rankTitle = new QLabel("<b>📋 How clipboard items are ranked</b>", this);
    mainLayout->addWidget(rankTitle);

    QLabel *rankText = new QLabel(
        "Each time you copy text, the item is added to the top of the history. "
        "When you <b>left-click</b> an item to paste it into the clipboard, "
        "that item's usage counter increases. The history is sorted by a <b>composite rating</b>:\n\n"
        "• <b>Favorites</b> always appear at the top, sorted by usage count among themselves.\n"
        "• Regular items are placed below favorites, also sorted by usage count.\n"
        "• The more often you use an item, the higher it stays in the list.\n\n"
        "This way, your most frequently used clips are always within easy reach.",
        this
    );
    rankText->setWordWrap(true);
    mainLayout->addWidget(rankText);

    mainLayout->addStretch();

    // --- Close button ---
    QPushButton *closeButton = new QPushButton("Close", this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addWidget(closeButton, 0, Qt::AlignRight);
}
