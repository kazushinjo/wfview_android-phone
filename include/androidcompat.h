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
#include <QApplication>
#include <QMainWindow>
#include <QPushButton>
#include <QScroller>
#include <QScreen>
#include <QWindow>

// Responsive scaling: every Android-specific fixed pixel amount in the code
// is expressed in "design pixels" against the original 2400x1378 reference
// layout, and mapped to real device pixels at runtime. On the reference
// tablet (2000x1200) this reproduces the tuned look exactly; on any other
// resolution/aspect the same proportions are kept while Qt's layouts absorb
// the remaining difference natively (no bitmap scaling of the main window).
inline qreal androidUiScale()
{
    static qreal s = 0.0;
    if (s <= 0.0) {
        const QScreen *scr = QGuiApplication::primaryScreen();
        if (scr == Q_NULLPTR)
            return 1.0;
        const QSizeF g = scr->geometry().size();
        // The app is locked to landscape but the screen geometry can still
        // report the natural (portrait) orientation at startup.
        const qreal w = qMax(g.width(), g.height());
        const qreal h = qMin(g.width(), g.height());
        s = qMin(w / 2400.0, h / 1378.0);
    }
    return s;
}

inline int androidDp(int designPx) { return qRound(designPx * androidUiScale()); }
inline qreal androidDpF(qreal designPx) { return designPx * androidUiScale(); }

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
    AndroidFitToScreenView(QGraphicsScene *scene, QSize naturalSize,
                           bool uniformScale = false, bool scrollVertical = false)
        : QGraphicsView(scene), naturalSize(naturalSize), uniform(uniformScale),
          scrollY(scrollVertical) {}

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
            // Use the real bounding rect; anchoring at (0,0) would clip the
            // left/top edge when a widget pokes left of the origin.
            const QRectF r = scene()->itemsBoundingRect();
            if (!r.isEmpty()) {
                s = r.size();
                setSceneRect(r);
            }
        }
        if (s.width() > 0 && s.height() > 0) {
            qreal sx = qreal(viewport()->width()) / s.width();
            qreal sy = qreal(viewport()->height()) / s.height();
            if (uniform) {
                // Popups: keep the widget's own proportions and never blow
                // small dialogs up into billboard text; letterbox instead.
                // A vertically scrolling popup ignores the height limit:
                // it keeps the standard popup text size and pans instead.
                const qreal u = scrollY ? qMin(sx, (qreal)1.3)
                                        : qMin(qMin(sx, sy), (qreal)1.3);
                sx = sy = u;
            }
            setTransform(QTransform::fromScale(sx, sy));
        }
    }

private:
    QSize naturalSize;
    bool uniform {false};
    bool scrollY {false};
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
// Bring the main fullscreen view back to the front. Android can leave its
// surface stale (uniform grey) after another top-level window is dismissed,
// so it is re-presented explicitly BEFORE the popup is hidden.
inline void androidPresentMainView(QWidget *except)
{
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget *tlw : topLevels) {
        if (tlw != except && tlw->isVisible()
            && (qobject_cast<QGraphicsView*>(tlw) != Q_NULLPTR
                || qobject_cast<QMainWindow*>(tlw) != Q_NULLPTR)) {
            tlw->showFullScreen();
            tlw->raise();
            tlw->activateWindow();
            tlw->update();
            // For an already-visible window showFullScreen() is a no-op, so
            // additionally schedule a real frame on the native surface --
            // without this the main window can stay stale (grey/black)
            // after another top-level window is dismissed on Android.
            if (tlw->windowHandle() != Q_NULLPTR)
                tlw->windowHandle()->requestUpdate();
        }
    }
}

inline QWidget *androidFitToScreen(QWidget *w, bool uniformScale = false,
                                   bool scrollVertical = false)
{
    static QHash<QWidget*, QWidget*> wrapped;
    auto it = wrapped.constFind(w);
    if (it != wrapped.constEnd()) {
        // Defensive: if a selection handler hid the embedded widget (e.g.
        // trying to close its popup via window()->hide()), an already-
        // wrapped popup would otherwise reopen as an empty scene.
        w->show();
        return it.value();
    }

    w->ensurePolished();
    w->adjustSize();
    QSize naturalSize = w->size();

    QGraphicsScene *scene = new QGraphicsScene();
    QGraphicsProxyWidget *proxy = scene->addWidget(w);

    AndroidFitToScreenView *view = new AndroidFitToScreenView(scene, naturalSize,
                                                              uniformScale, scrollVertical);
    if (scrollVertical) {
        // Finger panning for content taller than the screen.
        QScroller::grabGesture(view->viewport(), QScroller::TouchGesture);
        view->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    }
    // Track later growth of the embedded widget so nothing gets scaled
    // off-screen.
    QObject::connect(proxy, &QGraphicsWidget::geometryChanged, view,
                     [view]() { view->refit(); });
    view->setFrameShape(QFrame::NoFrame);
    view->setBackgroundBrush(QColor(25, 27, 30));
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setSceneRect(0, 0, naturalSize.width(), naturalSize.height());

    if (uniformScale) {
        // Every popup gets the same modest back button, drawn on the
        // wrapper itself (unscaled) at the top-left.
        QPushButton *back = new QPushButton(QStringLiteral("← 戻る"), view);
        back->setObjectName(QStringLiteral("wrapperBackButton"));
        back->setFixedSize(androidDp(156), androidDp(58));
        back->move(androidDp(14), androidDp(10));
        back->raise();
        QObject::connect(back, &QPushButton::clicked, view, [view]() {
            androidPresentMainView(view);
            view->hide();
        });
    }

    wrapped.insert(w, view);
    return view;
}
#endif // Q_OS_ANDROID

#endif // ANDROIDCOMPAT_H
