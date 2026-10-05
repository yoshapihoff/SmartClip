#include "HelpDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFont>
#include <QScrollArea>

HelpDialog::HelpDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    setWindowTitle("Help — SmartClip");
    setMinimumWidth(520);
    // Справка длинная — даём окну вменяемый размер, остальное прокручивается.
    resize(560, 640);
    setModal(true);
}

void HelpDialog::setupUI()
{
    // Текст лежит в отдельном виджете внутри QScrollArea, чтобы на низких
    // экранах его не обрезало снизу.
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setSpacing(12);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *content = new QWidget(scroll);
    QVBoxLayout *mainLayout = new QVBoxLayout(content);
    mainLayout->setSpacing(12);

    scroll->setWidget(content);
    outerLayout->addWidget(scroll, 1);

    // --- Заголовок ---
    QLabel *titleLabel = new QLabel("SmartClip — quick reference", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    // --- 0. Как устроено меню ---
    QLabel *menuTitle = new QLabel("<b>Menu: how the modes work</b>", this);
    mainLayout->addWidget(menuTitle);

    QLabel *menuText = new QLabel(
        "The tray menu has four mode items — <b>Favorite mode</b>, "
        "<b>Reveal passwords</b>, <b>Comments</b> and <b>Delete mode</b>. They "
        "carry no icons: the mode is on when the item shows a <b>checkmark</b> "
        "and off when the checkmark is absent. Click the item to toggle the mode.\n\n"
        "<b>Every mode is one-shot.</b> Turn a mode on, click one history item to "
        "apply its action, and the mode switches itself off again (the checkmark "
        "disappears). For Comments the mode clears when the dialog closes — "
        "whether you press <b>OK</b> or <b>Cancel</b>.\n\n"
        "With all modes off, a <b>left-click</b> on an item simply copies it to "
        "the clipboard.",
        this
    );
    menuText->setWordWrap(true);
    mainLayout->addWidget(menuText);

    // --- 1. Избранное ---
    QLabel *favTitle = new QLabel("<b>1. Favorite mode — pin an item</b>", this);
    mainLayout->addWidget(favTitle);

    QLabel *favText = new QLabel(
        "Turn on <b>Favorite mode</b> in the menu, then <b>left-click</b> the item "
        "you want to pin. A colored dot appears next to it and the mode turns off. "
        "Repeat the same steps on a pinned item to remove it from favorites.\n\n"
        "Favorites stay pinned at the top of the history so you can always find them "
        "quickly.",
        this
    );
    favText->setWordWrap(true);
    mainLayout->addWidget(favText);

#if defined(Q_OS_MAC)
    QLabel *favMac = new QLabel(
        "On macOS you can also hold <b>Ctrl</b> (⌃) and <b>left-click</b> an item "
        "to toggle its favorite state in one go, without entering the mode.",
        this
    );
    favMac->setWordWrap(true);
    mainLayout->addWidget(favMac);
#elif defined(Q_OS_LINUX)
    QLabel *favX11 = new QLabel(
        "On X11 you can also hold <b>Ctrl</b> and <b>left-click</b> an item to toggle "
        "its favorite state without entering the mode. On Wayland that key modifier "
        "never reaches the application, so use the mode instead.",
        this
    );
    favX11->setWordWrap(true);
    mainLayout->addWidget(favX11);
#endif

    // --- 2. Вскрытие / сокрытие паролей ---
    QLabel *revealTitle = new QLabel("<b>2. Reveal passwords — reveal or mask text</b>", this);
    mainLayout->addWidget(revealTitle);

    QLabel *revealText = new QLabel(
        "Turn on <b>Reveal passwords</b> in the menu, then <b>left-click</b> an item: "
        "a masked item is revealed in full, and a normal item is masked so that only "
        "its first and last characters stay visible (the rest becomes asterisks: ***). "
        "The mode turns off after that single click.\n\n"
        "Masked items are also masked in the menu itself, so glanced-at passwords stay "
        "hidden. With the mode off, clicking a masked item just copies the full text "
        "to the clipboard.",
        this
    );
    revealText->setWordWrap(true);
    mainLayout->addWidget(revealText);

#if defined(Q_OS_MAC)
    QLabel *revealMac = new QLabel(
        "On macOS you can also hold <b>Shift</b> (⇧) and <b>left-click</b> an item to "
        "toggle its masked state without entering the mode.",
        this
    );
    revealMac->setWordWrap(true);
    mainLayout->addWidget(revealMac);
#endif

    // --- 3. Комментарии ---
    QLabel *cmtTitle = new QLabel("<b>3. Comments — attach a note to an item</b>", this);
    mainLayout->addWidget(cmtTitle);

    QLabel *cmtText = new QLabel(
        "Turn on <b>Comments</b> in the menu, then <b>left-click</b> an item. A small "
        "window opens with a text field (pre-filled with the existing note, if any). "
        "Type a note and press <b>OK</b> to save it, or <b>Cancel</b> to discard. Either "
        "way the mode turns off and the comment appears right after the item text, "
        "separated by an em dash (\u2014 note).",
        this
    );
    cmtText->setWordWrap(true);
    mainLayout->addWidget(cmtText);

    // --- 4. Удаление ---
    QLabel *delTitle = new QLabel("<b>4. Delete mode — remove an item</b>", this);
    mainLayout->addWidget(delTitle);

    QLabel *delText = new QLabel(
        "Turn on <b>Delete mode</b> in the menu, then <b>left-click</b> an item: it is "
        "removed from the history and the mode switches off. Deletion is propagated to "
        "your other devices if network sync is enabled — in both directions, regardless "
        "of which device is master.",
        this
    );
    delText->setWordWrap(true);
    mainLayout->addWidget(delText);

    // --- 5. Копирование и порядок в истории ---
    QLabel *rankTitle = new QLabel("<b>5. Copying and how items are ranked</b>", this);
    mainLayout->addWidget(rankTitle);

    QLabel *rankText = new QLabel(
        "Each time you copy text, the item is added to the top of the history. When you "
        "<b>left-click</b> an item to send it back to the clipboard, that item's usage "
        "counter increases. The history is sorted by a composite rating:\n\n"
        "• Favorites always appear at the top, sorted among themselves by usage count.\n"
        "• Regular items follow below, also sorted by usage count.\n"
        "• The more often you use an item, the higher it stays in the list.\n\n"
        "This way your most frequently used clips are always within easy reach.",
        this
    );
    rankText->setWordWrap(true);
    mainLayout->addWidget(rankText);

    // --- 6. Безопасность / хранение ---
    QLabel *secTitle = new QLabel("<b>6. Storage and security</b>", this);
    mainLayout->addWidget(secTitle);

    QLabel *secText = new QLabel(
        "History, comments and masking flags are encrypted at rest with "
        "<b>AES-256-GCM</b>. The key lives in the system key store "
#if defined(Q_OS_MAC)
        "(macOS Keychain)"
#elif defined(Q_OS_LINUX)
        "(Secret Service / GNOME Keyring)"
#else
        "(system key store)"
#endif
        " and the history file contains only ciphertext. <b>Clear</b> in the menu wipes "
        "the whole history; <b>Settings</b> controls the history size.",
        this
    );
    secText->setWordWrap(true);
    mainLayout->addWidget(secText);

    mainLayout->addStretch();

    // --- Close button (вне области прокрутки — всегда видна) ---
    QPushButton *closeButton = new QPushButton("Close", this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    outerLayout->addWidget(closeButton, 0, Qt::AlignRight);
}
