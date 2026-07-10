#include "androidcombobox.h"

#include <QComboBox>
#include <QListWidget>
#include <QEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLineEdit>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

namespace {

// Width of the drop-down arrow hot zone on the right edge of an editable
// combo box; taps left of it belong to the embedded line edit.
constexpr int kArrowWidth = 40;

// Closes the popup list on any press outside it (including back on the
// combo box itself). A press *inside* the list is deliberately left
// unconsumed so QListWidget's own viewport/scrollbar still gets it - taps
// are only turned into a selection on release, and only if the release
// landed close to where the press started, so a drag (scrolling a list
// longer than fits on screen) is not misread as picking whatever item is
// under the finger when it lifts. QListWidget's own press+release
// "clicked" detection needs a mouse grab to reliably match release to
// press, which is unreliable under Android touch input (same root cause
// as the native QComboBox popup itself), hence handling both here.
class DismissOnOutsideClick : public QObject
{
public:
    DismissOnOutsideClick(QListWidget *list, QComboBox *combo) : QObject(list), m_list(list), m_combo(combo) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched);
        if (!m_list) {
            qApp->removeEventFilter(this);
            deleteLater();
            return false;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            const QPoint globalPos = me->globalPosition().toPoint();
            const QPoint listLocal = m_list->mapFromGlobal(globalPos);
            const bool inside = m_list->rect().contains(listLocal);
            if (!inside) {
                qApp->removeEventFilter(this);
                m_list->close();
                // Only swallow the dismissing press when it lands on the
                // combo box itself (minus the editable combo's line edit):
                // swallowing there is what makes tapping the arrow *close*
                // the list instead of ComboPopupFilter immediately
                // reopening it. Everywhere else the press must be left
                // unconsumed: the Android QPA only grants focus on the
                // *release* of a tap (setFocusOnTouchRelease), and
                // QApplicationPrivate::giveFocusAccordingToFocusPolicy()
                // refuses to move focus when that release arrives without
                // its matching widget-level press - which is exactly what
                // eating the press here used to cause. The visible symptom
                // was an editable combo's line edit popping up the soft
                // keyboard while focus never reached the combo box, so no
                // text could ever be typed.
                if (m_combo) {
                    const QPoint comboLocal = m_combo->mapFromGlobal(globalPos);
                    if (m_combo->rect().contains(comboLocal)) {
                        QLineEdit *edit = m_combo->lineEdit();
                        const bool onLineEdit = edit
                            && edit->rect().contains(edit->mapFromGlobal(globalPos));
                        if (!onLineEdit)
                            return true;
                    }
                }
                return false;
            }
            m_pressPos = globalPos;
            m_pressValid = true;
            return false;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            if (!m_pressValid)
                return false;
            m_pressValid = false;
            auto *me = static_cast<QMouseEvent *>(event);
            const QPoint globalPos = me->globalPosition().toPoint();
            if ((globalPos - m_pressPos).manhattanLength() > 16)
                return false; // treat as a scroll drag, not a tap
            selectItemAt(globalPos);
            return true;
        }
        return false;
    }

private:
    void selectItemAt(const QPoint &globalPos)
    {
        const QPoint viewportLocal = m_list->viewport()->mapFromGlobal(globalPos);
        QListWidgetItem *item = m_list->itemAt(viewportLocal);
        if (!item)
            return;
        const int newIndex = item->data(Qt::UserRole).toInt();
        qApp->removeEventFilter(this);
        if (m_combo) {
            m_combo->setCurrentIndex(newIndex);
            // setCurrentIndex() does not emit activated(), but the main
            // window's handlers (on_preampSelCombo_activated etc.) are
            // wired to it -- that is what actually sends the rig command.
            // Emit it exactly like a native popup pick would.
            QMetaObject::invokeMethod(m_combo, "activated", Q_ARG(int, newIndex));
        }
        m_list->close();
        // The list closing/deleting does not reliably trigger a repaint of
        // the screen region it used to cover on this build, leaving the
        // combo box's old text visually stuck even though its internal
        // state already changed correctly. Force the whole host window to
        // repaint immediately.
        if (m_combo) {
            QWidget *host = m_combo->window();
            if (host) {
                host->update();
                host->repaint();
            }
            m_combo->update();
            m_combo->repaint();
        }
    }

    QPointer<QListWidget> m_list;
    QPointer<QComboBox> m_combo;
    QPoint m_pressPos;
    bool m_pressValid = false;
};

class ComboPopupFilter : public QObject
{
public:
    explicit ComboPopupFilter(QComboBox *combo) : QObject(combo), m_combo(combo) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (m_combo->isEditable() && watched == m_combo->lineEdit())
            return lineEditEventFilter(event);
        if (watched != m_combo)
            return false;
        if (event->type() == QEvent::MouseButtonRelease) {
            // Keep releases away from QComboBox::mouseReleaseEvent(): under
            // the Android QPA (setFocusOnTouchRelease) it calls showPopup()
            // whenever a focused combo receives a release on its arrow,
            // opening the native Qt::Popup that never gets a rendering
            // surface on this build (see include/androidcombobox.h) - an
            // invisible window whose popup grab then swallows every
            // subsequent tap and key event. The editable combo's text area
            // keeps its releases (they normally go to the line edit anyway).
            auto *me = static_cast<QMouseEvent *>(event);
            return !m_combo->isEditable()
                || me->pos().x() >= m_combo->width() - kArrowWidth;
        }
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick)
            return false;

        if (m_combo->isEditable()) {
            // Editable combo boxes still need their line edit for typing;
            // only the drop-down arrow on the right opens the list of
            // existing entries.
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->pos().x() < m_combo->width() - kArrowWidth)
                return false;
        }
        showList();
        return true;
    }

private:
    // Installed on the editable combo's line edit (see
    // installAndroidComboBoxFix). The Android QPA only grants focus on the
    // release of a tap, and QApplicationPrivate::giveFocusAccordingToFocusPolicy()
    // skips even that whenever its press/release bookkeeping got out of
    // sync (a press consumed by DismissOnOutsideClick, focus moved while
    // the finger was down, ...). QLineEdit still shows the soft keyboard
    // from its own release handler in that state, leaving the user with a
    // keyboard that types into whatever stale widget has focus. Grant
    // focus to the combo (the line edit's focus proxy) ourselves on every
    // release, and request the input panel once focus has settled.
    bool lineEditEventFilter(QEvent *event)
    {
        if (event->type() != QEvent::MouseButtonRelease)
            return false;
        if (!m_combo->hasFocus())
            m_combo->setFocus(Qt::MouseFocusReason);
        // Match native Android behavior: a tap on a text field always
        // brings up the keyboard. QLineEdit's own release handler only
        // requests it when the field was focused *before* the tap
        // (RSIP_OnMouseClickAndAlreadyFocused), so the first tap after
        // focus was lost elsewhere would leave the user without one.
        QPointer<QComboBox> comboGuard(m_combo);
        QTimer::singleShot(0, m_combo, [comboGuard]() {
            if (comboGuard && comboGuard->hasFocus())
                QGuiApplication::inputMethod()->show();
        });
        return false;
    }

    // Comboboxes nested inside an embedded overlay widget need their popup
    // parented to that overlay, not all the way up to the top-level window:
    // the top-level can have other children that may get raised after our
    // list is shown, burying a list parented directly to it behind the
    // overlay the combo actually lives in. Walk up to the ancestor whose
    // own parent is the top-level window, i.e. one level short of window().
    static QWidget *nearestOverlayHost(QComboBox *combo)
    {
        QWidget *top = combo->window();
        QWidget *w = combo->parentWidget();
        QWidget *candidate = top;
        while (w && w != top) {
            candidate = w;
            w = w->parentWidget();
        }
        return candidate ? candidate : top;
    }

    void showList()
    {
        if (!m_combo->isEnabled() || m_combo->count() == 0)
            return;

        QWidget *host = nearestOverlayHost(m_combo);
        if (!host)
            return;

        auto *list = new QListWidget(host);
        list->setAttribute(Qt::WA_DeleteOnClose);
        list->setWindowFlags(Qt::Widget);
        list->setFocusPolicy(Qt::StrongFocus);

        for (int i = 0; i < m_combo->count(); ++i) {
            auto *item = new QListWidgetItem(m_combo->itemText(i), list);
            item->setData(Qt::UserRole, i);
        }
        list->setCurrentRow(m_combo->currentIndex());

        const int rowH = qMax(list->sizeHintForRow(0), 32);
        const int visibleRows = qMin(m_combo->count(), 8);
        const int listHeight = rowH * visibleRows + 2 * list->frameWidth() + 4;
        const int listWidth = qMax(m_combo->width(), 200);

        const QPoint comboTopLeftInHost = host->mapFromGlobal(m_combo->mapToGlobal(QPoint(0, 0)));
        int x = qBound(0, comboTopLeftInHost.x(), qMax(0, host->width() - listWidth));
        int y = comboTopLeftInHost.y() + m_combo->height();
        if (y + listHeight > host->height())
            y = qMax(0, comboTopLeftInHost.y() - listHeight);

        list->setGeometry(x, y, listWidth, listHeight);
        list->show();
        list->raise();
        list->setFocus(Qt::PopupFocusReason);

        // Defer installing the outside-click dismiss filter until after this
        // event (and any related synthetic press/release events for the same
        // touch) has finished processing, so the tap that opened the list
        // cannot also be seen as the "outside click" that immediately closes
        // it again.
        QPointer<QListWidget> listGuard(list);
        QPointer<QComboBox> comboGuard(m_combo);
        QTimer::singleShot(0, list, [listGuard, comboGuard]() {
            if (!listGuard)
                return;
            qApp->installEventFilter(new DismissOnOutsideClick(listGuard, comboGuard));
        });
    }

    QComboBox *m_combo;
};

} // namespace

void installAndroidComboBoxFix(QComboBox *combo)
{
    if (!combo)
        return;
    auto *filter = new ComboPopupFilter(combo);
    combo->installEventFilter(filter);
    // Editable combos also need the filter on their line edit so a tap
    // there always ends up focusing the combo - see
    // ComboPopupFilter::lineEditEventFilter().
    if (combo->lineEdit() != nullptr)
        combo->lineEdit()->installEventFilter(filter);
}
