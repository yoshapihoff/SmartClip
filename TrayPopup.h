#pragma once

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QIcon>

class QFrame;
class QVBoxLayout;
class QLabel;
class QScrollArea;
class QPushButton;

/**
 * TrayPopup — СВОЁ всплывающее окно вместо меню десктопа.
 *
 * Зачем: на GNOME (и частично на других DE) меню трея рисует оболочка по
 * dbusmenu, и отступы/вид внутри меню приложению неподконтрольны. Здесь мы
 * рисуем своё окно Qt (Qt::Popup, без рамки и системных кнопок) с нашими
 * отступами — одинаково на Linux и macOS.
 *
 * Состав (паритет с меню):
 *   • список истории: клик — копировать;
 *   • кнопки строки: ★ избранное, 👁 маска, ✎ комментарий, ✕ удалить
 *     (иконки рисуются векторно, не зависят от шрифтов);
 *   • футер: Hide / Clear / Settings / Help / About / Quit.
 */
class TrayPopup final : public QWidget
{
    Q_OBJECT

public:
    /** Один пункт списка. Готовится вызывающей стороной. */
    struct RowData {
        QString text;            // полный текст (кладём в буфер)
        QString display;         // как показывать (маска)
        QString comment;         // комментарий (отдельным нежирным элементом)
        bool favorite = false;
        QColor color;            // цвет маркера избранного (если favorite)
        bool masked = false;     // сейчас ли маскируется (для иконки «глаз»)
        /** Доступна ли смена раскладки текста (в системе ≥ 2 раскладок).
         *  false — кнопка раскладки в строке не показывается. */
        bool layoutConvertible = false;
        /**
         * Строка — ИСХОДНИК последней смены раскладки: исправленный текст уже
         * лежит в буфере (полоса сверху). Показывается клавиатура-акцентом,
         * чтобы было видно, откуда именно получился текст в буфере.
         */
        bool convertedSource = false;
    };

    explicit TrayPopup(QWidget *parent = nullptr);

    void setRows(const QVector<RowData> &rows);
    void setDarkMode(bool dark);
    void setVersion(const QString &version);

    /** Показать рядом с точкой anchor (позиция иконки трея/курсора). */
    void showAt(const QPoint &anchor);

    /**
     * Полоса «исправленный текст в буфере». Смена раскладки кладёт новый текст
     * в буфер СРАЗУ, а полоса (а) подтверждает это, (б) служит кнопкой
     * «вставить в прежнее окно»: клик возвращает фокус приложению и жмёт
     * Ctrl/Cmd+V. Пустой текст — полосу убрать.
     */
    void setConvertedText(const QString &text);
    /** О чём сейчас сообщает полоса (пусто — полосы нет). */
    QString convertedText() const { return m_convertedText; }

signals:
    void clipChosen(const QString &text);
    void favoriteToggled(const QString &text);
    /** Сменить раскладку текста записи (перекодировать в следующую). */
    void layoutConvertRequested(const QString &text);
    void maskToggled(const QString &text);
    void commentRequested(const QString &text);
    void deleteRequested(const QString &text);
    void clearRequested();
    void settingsRequested();
    void helpRequested();
    void aboutRequested();
    void quitRequested();
    /** Вставить текст в ПРЕЖНЕЕ активное окно (клик по полосе смены раскладки). */
    void pasteIntoPreviousRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void applyStyle();
    void clearRows();
    /** Подогнать ширину окна под содержимое (без обрезки). */
    void updateContentWidth();
    /**
     * Выставить размер окна под содержимое и экран: ширина — по содержимому,
     * высота — по числу строк, но НЕ больше 2/3 высоты доступной области
     * экрана (при превышении появляется вертикальный скроллбар).
     */
    void updateContentSize(const QRect &availableArea);
    QIcon glyph(const QString &kind, const QColor &color, bool filled = false) const;
    /** Число кнопок-действий в строке (растёт, если есть смена раскладки). */
    int actionColumnCount() const;

    QFrame *m_card = nullptr;
    QFrame *m_sep = nullptr;
    QScrollArea *m_scroll = nullptr;   // список истории (скрыт при пустой истории)
    QWidget *m_footer = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_empty = nullptr;
    bool m_dark = false;
    QString m_version;
    // Последние установленные строки. Нужны, чтобы пересобрать их при смене
    // темы на лету (цвет текста строк задаётся явно из палитры, а QSS его
    // не перекрашивает).
    QVector<RowData> m_rows;
    /** Показывать ли кнопку смены раскладки (≥ 2 раскладок в системе). */
    bool m_layoutConvert = false;
    /** Полоса «исправленный текст в буфере» (показывается только при непустом). */
    QWidget *m_convertedBar = nullptr;
    QLabel *m_convertedLabel = nullptr;
    QPushButton *m_convertedBtn = nullptr;
    QString m_convertedText;
};
