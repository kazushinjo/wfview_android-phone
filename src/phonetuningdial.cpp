#include "phonetuningdial.h"

#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QtMath>

phoneTuningDial::phoneTuningDial(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void phoneTuningDial::setCenterText(const QString &text)
{
    centerText = text;
    update();
}

double phoneTuningDial::angleAt(const QPointF &p) const
{
    QPointF c = QRectF(rect()).center();
    // atan2 with x/y swapped and y negated: 0 at 12 o'clock, growing clockwise.
    return qRadiansToDegrees(qAtan2(p.x() - c.x(), c.y() - p.y()));
}

void phoneTuningDial::mousePressEvent(QMouseEvent *e)
{
    dragging = true;
    lastAngle = angleAt(e->position());
}

void phoneTuningDial::mouseMoveEvent(QMouseEvent *e)
{
    if (!dragging)
        return;
    QPointF c = QRectF(rect()).center();
    QPointF d = e->position() - c;
    // Near the centre the angle swings wildly for tiny finger movements.
    if (qSqrt(d.x() * d.x() + d.y() * d.y()) < qMin(width(), height()) * 0.12)
        return;

    double a = angleAt(e->position());
    double delta = a - lastAngle;
    if (delta > 180.0)  delta -= 360.0;
    if (delta < -180.0) delta += 360.0;
    lastAngle = a;

    accum += delta;
    rotation += delta;
    int steps = int(accum / degreesPerStep);
    if (steps != 0) {
        accum -= steps * degreesPerStep;
        emit stepped(steps);
    }
    update();
}

void phoneTuningDial::mouseReleaseEvent(QMouseEvent *)
{
    dragging = false;
    accum = 0.0;
}

void phoneTuningDial::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal side = qMin(width(), height());
    const QPointF c = QRectF(rect()).center();
    const qreal r = side / 2.0 - 2.0;

    // Tick ring around the knob.
    p.setPen(QPen(QColor("#5c6b75"), qMax<qreal>(1.0, side / 160.0)));
    for (int i = 0; i < 50; i++) {
        double t = qDegreesToRadians(i * 7.2);
        qreal r1 = (i % 5 == 0) ? r * 0.88 : r * 0.92;
        p.drawLine(QPointF(c.x() + r1 * qSin(t), c.y() - r1 * qCos(t)),
                   QPointF(c.x() + r  * qSin(t), c.y() - r  * qCos(t)));
    }

    // Knob body.
    const qreal kr = r * 0.84;
    QRadialGradient g(c.x() - kr * 0.3, c.y() - kr * 0.3, kr * 1.6);
    g.setColorAt(0.0, QColor("#6b7882"));
    g.setColorAt(0.5, QColor("#3a444c"));
    g.setColorAt(1.0, QColor("#1c2228"));
    p.setBrush(g);
    p.setPen(QPen(QColor("#0e1216"), 2));
    p.drawEllipse(c, kr, kr);

    // Finger dimple, rotating with the knob.
    double t = qDegreesToRadians(rotation);
    QPointF dimple(c.x() + kr * 0.68 * qSin(t), c.y() - kr * 0.68 * qCos(t));
    const qreal dr = kr * 0.16;
    QRadialGradient dg(dimple, dr);
    dg.setColorAt(0.0, QColor("#141a1f"));
    dg.setColorAt(1.0, QColor("#2c353c"));
    p.setBrush(dg);
    p.setPen(QPen(QColor("#7d8a94"), 1));
    p.drawEllipse(dimple, dr, dr);

    // Step size in the middle.
    if (!centerText.isEmpty()) {
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(qMax(10, int(kr * 0.22)));
        p.setFont(f);
        p.setPen(QColor("#e6e8ea"));
        p.drawText(QRectF(c.x() - kr * 0.5, c.y() - kr * 0.2, kr, kr * 0.4),
                   Qt::AlignCenter, centerText);
    }
}
