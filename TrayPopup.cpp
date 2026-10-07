#include "TrayPopup.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QScreen>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QToolButton>
#include <QFontMetrics>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QLayoutItem>
#include <QPen>
#include <cmath>
#include <QDebug>
#include <QEvent>

namespace {

// Стили для светлой/тёмной темы (близко к виду GNOME/Adwaita).
struct Palette {
    QString cardBg, cardBorder, titleFg, itemFg, itemHover,
            accent, accentFg, muted, danger, sep, iconFg;
};

Palette lightPalette()
{
    return {QStringLiteral("#fafafb"), QStringLiteral("#e6e6eb"),
            QStringLiteral("#66666e"), QStringLiteral("#222226"),
            QStringLiteral("#ececf0"), QStringLiteral("#3584e4"),
            QStringLiteral("#ffffff"), QStringLiteral("#9a9aa0"),
            QStringLiteral("#e62d42"), QStringLiteral("#e6e6eb"),
            QStringLiteral("#44444c")};
}

Palette darkPalette()
{
    return {QStringLiteral("#2b2b30"), QStringLiteral("#3a3a41"),
            QStringLiteral("#b8b8c0"), QStringLiteral("#f2f2f5"),
            QStringLiteral("#3a3a41"), QStringLiteral("#3584e4"),
            QStringLiteral("#ffffff"), QStringLiteral("#9a9aa0"),
            QStringLiteral("#ff6b6b"), QStringLiteral("#3a3a41"),
            QStringLiteral("#d0d0d8")};
}

constexpr int kActionWidth = 24;
constexpr int kRowHeight = 24;

} // namespace

/** Текст строки с эллипсисом и маркером избранного. Клик — копировать. */
class ElidedLabel final : public QWidget
{
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QWidget(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setMinimumHeight(kRowHeight);
    }
    void setFullText(const QString &t) { m_text = t; update(); }
    void setMarker(const QColor &c) { m_marker = c; update(); }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Резервируем колонку маркера ВСЕГДА, чтобы текст строк с избранным
        // и без него начинался на одной вертикали.
        const int left = 24;
        if (m_marker.isValid()) {
            p.setBrush(m_marker);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(12, height() / 2.0), 4, 4);
        }
        p.setPen(palette().color(QPalette::WindowText));
        const QFontMetrics fm(font());
        const int w = width() - left - 6;
        const QString el = fm.elidedText(m_text, Qt::ElideRight, qMax(0, w));
        p.drawText(QRect(left, 1, w, height() - 2),
                   Qt::AlignVCenter | Qt::AlignLeft, el);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()))
            emit clicked();
        QWidget::mouseReleaseEvent(e);
    }

private:
    QString m_text;
    QColor m_marker;
};

/** Контейнер строки: при наведении показывает кнопки-действия. */
class RowWidget final : public QWidget
{
    Q_OBJECT
public:
    RowWidget(QWidget *textWidget, QWidget *actions, const QString &hoverBg,
              QWidget *parent)
        : QWidget(parent), m_actions(actions), m_hoverBg(hoverBg)
    {
        auto *hl = new QHBoxLayout(this);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(2);
        hl->addWidget(textWidget, 1);
        hl->addWidget(actions, 0);
        const bool always =
            qEnvironmentVariableIsSet("SMARTCLIP_POPUP_ACTIONS_ALWAYS");
        m_actions->setVisible(always);
    }

protected:
    void enterEvent(QEnterEvent *e) override
    {
        m_actions->setVisible(true);
        setStyleSheet(QStringLiteral("#rowHost { background: %1; border-radius: 6px; }")
                          .arg(m_hoverBg));
        QWidget::enterEvent(e);
    }
    void leaveEvent(QEvent *e) override
    {
        if (!qEnvironmentVariableIsSet("SMARTCLIP_POPUP_ACTIONS_ALWAYS"))
            m_actions->setVisible(false);
        setStyleSheet(QString());
        QWidget::leaveEvent(e);
    }

private:
    QWidget *m_actions;
    QString m_hoverBg;
};

TrayPopup::TrayPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint
                          | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_card = new QFrame(this);
    m_card->setObjectName(QStringLiteral("card"));
    outer->addWidget(m_card);

    auto *lay = new QVBoxLayout(m_card);
    lay->setContentsMargins(6, 6, 6, 6);   // ОТСТУП ОТ КРАЯ (наш)
    lay->setSpacing(2);                     // ОТСТУП МЕЖДУ ЭЛЕМЕНТАМИ (наш)

    m_title = new QLabel(QStringLiteral("SmartClip"), m_card);
    m_title->setObjectName(QStringLiteral("title"));
    lay->addWidget(m_title);

    auto *scroll = new QScrollArea(m_card);
    scroll->setObjectName(QStringLiteral("scroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);

    auto *rowsHost = new QWidget(scroll);
    rowsHost->setObjectName(QStringLiteral("rowsHost"));
    m_rowsLayout = new QVBoxLayout(rowsHost);
    m_rowsLayout->setContentsMargins(0, 0, 5, 0);
    m_rowsLayout->setSpacing(1);
    m_rowsLayout->addStretch(1);

    scroll->setWidget(rowsHost);
    lay->addWidget(scroll, 1);

    m_empty = new QLabel(QStringLiteral("Clipboard history is empty"), m_card);
    m_empty->setObjectName(QStringLiteral("empty"));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->hide();
    lay->addWidget(m_empty);

    // ── Футер ─────────────────────────────────────────────────────────
    auto *sep = new QFrame(m_card);
    sep->setObjectName(QStringLiteral("sep"));
    sep->setFrameShape(QFrame::HLine);
    lay->addWidget(sep);

    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 2, 0, 0);
    footer->setSpacing(2);
    auto addFooter = [&](const QString &label, const QString &tip,
                         void (TrayPopup::*signal)()) {
        auto *btn = new QPushButton(label, m_card);
        btn->setObjectName(QStringLiteral("footerBtn"));
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFlat(true);
        connect(btn, &QPushButton::clicked, this, [this, signal]() {
            hide();
            emit (this->*signal)();
        });
        footer->addWidget(btn);
    };
    addFooter(QStringLiteral("Clear"), QStringLiteral("Clear history"),
              &TrayPopup::clearRequested);
    addFooter(QStringLiteral("Settings"), QStringLiteral("Settings"),
              &TrayPopup::settingsRequested);
    addFooter(QStringLiteral("Help"), QStringLiteral("Help"),
              &TrayPopup::helpRequested);
    addFooter(QStringLiteral("Quit"), QStringLiteral("Quit"),
              &TrayPopup::quitRequested);
    footer->addStretch(1);
    lay->addLayout(footer);

    applyStyle();
    resize(360, 420);
}

void TrayPopup::setVersion(const QString &version)
{
    m_version = version;
    m_title->setText(version.isEmpty()
                         ? QStringLiteral("SmartClip")
                         : QStringLiteral("SmartClip %1").arg(version));
}

void TrayPopup::setDarkMode(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    applyStyle();
}

void TrayPopup::applyStyle()
{
    const Palette p = m_dark ? darkPalette() : lightPalette();

    setStyleSheet(QStringLiteral(R"(
        #card {
            background: %1;
            border: 1px solid %2;
            border-radius: 12px;
        }
        #title { color: %3; font-size: 11px; padding: 2px 8px; }
        #empty { color: %5; padding: 18px 8px; }
        #sep { color: %10; }
        #scroll { background: transparent; border: none; }
        #rowsHost { background: transparent; }

        QPushButton#footerBtn {
            color: %4; background: transparent; border: none;
            border-radius: 6px; padding: 4px 10px;
        }
        QPushButton#footerBtn:hover { background: %6; }

        QToolButton#rowAction {
            background: transparent; border: none; border-radius: 5px;
            padding: 1px;
        }
        QToolButton#rowAction:hover { background: %6; }
        QToolButton#rowAction:checked { background: %7; }

        QScrollBar:vertical { width: 8px; background: transparent; margin: 0; }
        QScrollBar::handle:vertical {
            background: %5; border-radius: 4px; min-height: 24px;
            min-width: 8px;
        }
        QScrollBar::handle:vertical:hover { background: %4; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )")
    .arg(p.cardBg, p.cardBorder, p.titleFg, p.itemFg, p.muted, p.itemHover,
         p.accent, p.accentFg, p.danger, p.sep));

    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, QColor(p.itemFg));
    setPalette(pal);
}

QIcon TrayPopup::glyph(const QString &kind, const QColor &color,
                       bool filled) const
{
    const int S = 32;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    if (kind == QLatin1String("star")) {
        // Пятиконечная звезда. filled=false — контурная.
        QPainterPath path;
        const QPointF c(S / 2.0, S / 2.0);
        const qreal outer = 10.5, inner = 4.4;
        for (int i = 0; i < 10; ++i) {
            const qreal r = (i % 2 == 0) ? outer : inner;
            const qreal a = -M_PI / 2 + i * M_PI / 5.0;
            const QPointF pt(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        if (filled) {
            p.setBrush(color);
            p.setPen(Qt::NoPen);
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        }
        p.drawPath(path);
    } else if (kind == QLatin1String("eye")) {
        // Глаз: контур-линза + зрачок.
        QPainterPath path;
        path.moveTo(5, 16);
        path.cubicTo(11, 8, 21, 8, 27, 16);
        path.cubicTo(21, 24, 11, 24, 5, 16);
        p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(16, 16), 3.2, 3.2);
    } else if (kind == QLatin1String("eyeOff")) {
        // Тот же глаз + перечёркивание (замаскировано).
        QPainterPath path;
        path.moveTo(5, 16);
        path.cubicTo(11, 8, 21, 8, 27, 16);
        path.cubicTo(21, 24, 11, 24, 5, 16);
        p.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(16, 16), 3.2, 3.2);
        p.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(7, 25), QPointF(25, 7));
    } else if (kind == QLatin1String("pencil")) {
        // Карандаш под 45°.
        p.save();
        p.translate(16, 16);
        p.rotate(-45);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(-3, -11, 6, 17), 1.5, 1.5);   // корпус
        p.drawPolygon(QPolygonF({QPointF(-3, 6), QPointF(3, 6), QPointF(0, 11)}));
        p.restore();
    } else { // "close"
        p.setPen(QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(8, 8), QPointF(24, 24));
        p.drawLine(QPointF(24, 8), QPointF(8, 24));
    }
    p.end();
    (void)S;
    return QIcon(pm);
}

void TrayPopup::clearRows()
{
    while (m_rowsLayout->count() > 1) {
        QLayoutItem *it = m_rowsLayout->takeAt(0);
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
}

void TrayPopup::setRows(const QVector<RowData> &rows)
{
    clearRows();
    m_empty->setVisible(rows.isEmpty());

    const Palette p = m_dark ? darkPalette() : lightPalette();
    const QColor iconFg(p.iconFg);

    for (const RowData &r : rows) {
        auto *text = new ElidedLabel(m_card);
        text->setFullText(r.display);
        if (r.favorite && r.color.isValid())
            text->setMarker(r.color);
        connect(text, &ElidedLabel::clicked, this, [this, t = r.text]() {
            hide();
            emit clipChosen(t);
        });

        auto *actions = new QWidget(m_card);
        actions->setFixedWidth(kActionWidth * 4 + 6);
        auto *al = new QHBoxLayout(actions);
        al->setContentsMargins(0, 0, 0, 0);
        al->setSpacing(0);

        auto addAction = [&](const QString &glyphKind, const QString &tip,
                             bool active, const QColor &tint,
                             void (TrayPopup::*signal)(const QString &)) {
            auto *b = new QToolButton(actions);
            b->setObjectName(QStringLiteral("rowAction"));
            // Активная опция (избранное / маска) — акцентным цветом, заливкой;
            // обычное состояние — контуром в основном цвете иконок.
            b->setIcon(glyph(glyphKind, active ? p.accent : tint, active));
            b->setIconSize(QSize(16, 16));
            b->setToolTip(tip);
            b->setCursor(Qt::PointingHandCursor);
            b->setAutoRaise(true);
            b->setFixedSize(kActionWidth, kRowHeight - 2);
            connect(b, &QToolButton::clicked, this,
                    [this, t = r.text, signal]() { emit (this->*signal)(t); });
            al->addWidget(b);
        };

        const QColor danger(p.danger);
        addAction(QStringLiteral("star"), QStringLiteral("Favorite"), r.favorite,
                  iconFg, &TrayPopup::favoriteToggled);
        addAction(r.masked ? QStringLiteral("eyeOff") : QStringLiteral("eye"),
                  QStringLiteral("Mask / reveal"), false, iconFg,
                  &TrayPopup::maskToggled);
        addAction(QStringLiteral("pencil"), QStringLiteral("Comment"), false,
                  iconFg, &TrayPopup::commentRequested);
        addAction(QStringLiteral("close"), QStringLiteral("Delete"), false,
                  danger, &TrayPopup::deleteRequested);

        auto *row = new RowWidget(text, actions,
                                  m_dark ? QStringLiteral("#3a3a41")
                                         : QStringLiteral("#ececf0"),
                                  m_card);
        row->setObjectName(QStringLiteral("rowHost"));
        row->setFixedHeight(kRowHeight);

        m_rowsLayout->insertWidget(m_rowsLayout->count() - 1, row);
    }
}

void TrayPopup::showAt(const QPoint &anchor)
{
    QScreen *screen = QGuiApplication::screenAt(anchor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect area = screen->availableGeometry();

    // Встаём СРАЗУ ПОД точкой клика: левый край окна — от точки клика,
    // ниже — небольшой отступ. Если правый край выходит за экран — сдвигаем
    // влево. Если снизу не влезает — показываем НАД точкой клика.
    constexpr int kGap = 6;
    constexpr int kMargin = 6;

    int x = anchor.x();
    int y = anchor.y() + kGap;

    if (x + width() > area.right() - kMargin)
        x = area.right() - kMargin - width();
    if (x < area.left() + kMargin)
        x = area.left() + kMargin;

    if (y + height() > area.bottom() - kMargin)
        y = anchor.y() - kGap - height();
    if (y < area.top() + kMargin)
        y = area.top() + kMargin;

    move(x, y);
    show();
    raise();
    activateWindow();
    setFocus();
}

void TrayPopup::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void TrayPopup::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::transparent);
    QWidget::paintEvent(event);
}

bool TrayPopup::event(QEvent *event)
{
    if (qEnvironmentVariableIsSet("SMARTCLIP_POPUP_DEBUG")) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::WindowDeactivate:
        case QEvent::WindowActivate:
        case QEvent::FocusOut:
        case QEvent::FocusIn:
        case QEvent::Hide:
            qInfo() << "TrayPopup event:" << event->type();
            break;
        default:
            break;
        }
    }

    // Надёжное закрытие «как меню»: помимо штатного Qt::Popup (клик вне),
    // прячем окно, когда оно теряет активность окна/приложения. На части
    // Wayland/XWayland-сборок штатный захват мыши не срабатывает.
    if (event->type() == QEvent::WindowDeactivate ||
        event->type() == QEvent::ApplicationDeactivate) {
        if (isVisible())
            hide();
    }
    return QWidget::event(event);
}

#include "TrayPopup.moc"
