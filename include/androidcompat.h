#ifndef ANDROIDCOMPAT_H
#define ANDROIDCOMPAT_H

#include <QWidget>

// Qt::Dialog (the default top-level window type for QDialog and anything
// built on it, including QMessageBox) never receives touch input on Qt 6.8
// for Android -- confirmed with a minimal reproduction outside wfview.
// Call this on any QDialog/QMessageBox before show()/exec() on Android.
inline void androidFixDialogFocus(QWidget *dialog)
{
#ifdef Q_OS_ANDROID
    dialog->setWindowFlags(Qt::Window);
#endif
}

#ifdef Q_OS_ANDROID
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsProxyWidget>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTransform>
#include <QHash>

// wfview's various top-level windows (main screen, settings, band select,
// frequency entry, etc.) are all sized in fixed desktop-era pixel amounts
// and do not stretch to fill an arbitrary phone/tablet screen: shown
// directly, they either overflow off the physical screen edges or leave
// blank space, with no way to scroll to the rest. Scale the widget's
// natural size to exactly fill the screen via a QGraphicsView/
// QGraphicsProxyWidget, with the transform recomputed on every resize from
// the view's own viewport (not a screen-geometry query, which was observed
// to reflect the device's natural/portrait orientation instead of its
// current rotated one).
class AndroidFitToScreenView : public QGraphicsView
{
public:
    AndroidFitToScreenView(QGraphicsScene *scene, QSize naturalSize)
        : QGraphicsView(scene), naturalSize(naturalSize) {}

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QGraphicsView::resizeEvent(event);
        refit();
    }

    void showEvent(QShowEvent *event) override
    {
        QGraphicsView::showEvent(event);
        refit();
    }

public:
    // The wrapped widget's layout can legitimately grow after wrapping
    // (combo boxes get populated, rows become visible, controls are
    // inserted), so the size captured at wrap time goes stale and the
    // widget's lower part would be scaled out of view. Re-read the live
    // size from the scene on every fit instead.
    void refit()
    {
        QSizeF s = naturalSize;
        if (scene() != Q_NULLPTR) {
            const QRectF r = scene()->itemsBoundingRect();
            if (!r.isEmpty()) {
                s = r.size();
                setSceneRect(QRectF(QPointF(0, 0), s));
            }
        }
        if (s.width() > 0 && s.height() > 0) {
            const qreal sx = qreal(viewport()->width()) / s.width();
            const qreal sy = qreal(viewport()->height()) / s.height();
            setTransform(QTransform::fromScale(sx, sy));
        }
    }

private:
    QSize naturalSize;
};

// Wraps a plain top-level QWidget in an AndroidFitToScreenView the first
// time it's shown, and returns that view to show()/raise()/activateWindow()
// instead of the widget itself. Idempotent -- returns the same view on
// later calls for a widget that's already wrapped.
//
// Do not call this on a widget that has already been shown independently:
// Android gives a top-level widget its own native surface as soon as it's
// shown, and embedding it afterwards leaves that surface still compositing
// on top of the scaled proxy (confirmed with wfmain's QMainWindow, which is
// why wfmain uses takeCentralWidget() in main.cpp instead of wrapping
// itself -- its central widget was never shown/top-level on its own).
inline QWidget *androidFitToScreen(QWidget *w)
{
    static QHash<QWidget*, QWidget*> wrapped;
    auto it = wrapped.constFind(w);
    if (it != wrapped.constEnd())
        return it.value();

    w->ensurePolished();
    w->adjustSize();
    QSize naturalSize = w->size();

    QGraphicsScene *scene = new QGraphicsScene();
    QGraphicsProxyWidget *proxy = scene->addWidget(w);

    AndroidFitToScreenView *view = new AndroidFitToScreenView(scene, naturalSize);
    // Track later growth of the embedded widget so nothing gets scaled
    // off-screen.
    QObject::connect(proxy, &QGraphicsWidget::geometryChanged, view,
                     [view]() { view->refit(); });
    view->setFrameShape(QFrame::NoFrame);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setSceneRect(0, 0, naturalSize.width(), naturalSize.height());

    wrapped.insert(w, view);
    return view;
}
#endif // Q_OS_ANDROID

#endif // ANDROIDCOMPAT_H
