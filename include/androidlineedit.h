#ifndef ANDROIDLINEEDIT_H
#define ANDROIDLINEEDIT_H

#include <QLineEdit>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QEventLoop>

// Qt 6.8's Android backing store does not reliably repaint a QLineEdit
// while it holds IME focus: text only becomes visible once some later,
// unrelated repaint happens to flush it (e.g. losing focus). Calling
// repaint()/update() from a slot connected to textChanged does not fix
// this. This subclass instead forces a synchronous repaint, and pumps
// the event loop once, directly inside the same call stack as the
// input event itself, immediately after every keystroke and IME commit.
class AndroidLineEdit : public QLineEdit
{
public:
    explicit AndroidLineEdit(QWidget *parent = nullptr) : QLineEdit(parent)
    {
        // Every field promoted to this class holds a technical ASCII value
        // (hostname, port, username, password, hex color) -- never Japanese
        // text -- but the IME still defaults to the system input language
        // (e.g. romaji/kana conversion) unless told otherwise. This hints
        // it to offer a Latin/English layout instead.
        setInputMethodHints(inputMethodHints() | Qt::ImhLatinOnly | Qt::ImhNoPredictiveText);
    }

protected:
    void inputMethodEvent(QInputMethodEvent *event) override
    {
        QLineEdit::inputMethodEvent(event);
        forceRepaint();
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        QLineEdit::keyPressEvent(event);
        forceRepaint();
    }

private:
    void forceRepaint()
    {
        repaint();
        if (QWidget *top = window())
            top->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
};

#endif // ANDROIDLINEEDIT_H
