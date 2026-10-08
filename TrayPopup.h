#pragma once

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QIcon>

class QFrame;
class QVBoxLayout;
class QLabel;

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
 *   • футер: Clear / Settings / Help / Quit.
 */
class TrayPopup final : public QWidget
{
    Q_OBJECT

public:
    /** Один пункт списка. Готовится вызывающей стороной. */
    struct RowData {
        QString text;            // полный текст (кладём в буфер)
        QString display;         // как показывать (маска + «— комментарий»)
        bool favorite = false;
        QColor color;            // цвет маркера избранного (если favorite)
        bool masked = false;     // сейчас ли маскируется (для иконки «глаз»)
    };

    explicit TrayPopup(QWidget *parent = nullptr);

    void setRows(const QVector<RowData> &rows);
    void setDarkMode(bool dark);
    void setVersion(const QString &version);
    /**
     * Временное подавление авто-скрытия (WindowDeactivate). Нужно для
     * модальных диалогов, вызванных ИЗ попапа (комментарий): они забирают
     * фокус, а попап по требованию прятать не должен.
     */
    void setSuppressAutoHide(bool on) { m_suppressAutoHide = on; }

    /** Показать рядом с точкой anchor (позиция иконки трея/курсора). */
    void showAt(const QPoint &anchor);

signals:
    void clipChosen(const QString &text);
    void favoriteToggled(const QString &text);
    void maskToggled(const QString &text);
    void commentRequested(const QString &text);
    void deleteRequested(const QString &text);
    void clearRequested();
    void settingsRequested();
    void helpRequested();
    void quitRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void applyStyle();
    void clearRows();
    QIcon glyph(const QString &kind, const QColor &color, bool filled = false) const;

    QFrame *m_card = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_empty = nullptr;
    bool m_dark = false;
    bool m_suppressAutoHide = false;   // не прятаться при потере фокуса
    QString m_version;
};
