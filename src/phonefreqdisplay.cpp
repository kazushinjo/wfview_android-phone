#include "phonefreqdisplay.h"

#include <QMouseEvent>
#include <QPainter>
#include <QFontMetricsF>

phoneFreqDisplay::phoneFreqDisplay(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(48);
}

void phoneFreqDisplay::setFrequency(quint64 hz)
{
    freq = hz;
    haveFreq = true;
    update();
}

void phoneFreqDisplay::setStep(quint64 hz)
{
    step = hz;
    update();
}

void phoneFreqDisplay::setGroupSeparator(QChar sep)
{
    groupSep = sep;
    update();
}

void phoneFreqDisplay::clear()
{
    haveFreq = false;
    update();
}

void phoneFreqDisplay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont big = font();
    big.setBold(true);
    big.setPixelSize(36);
    QFont unit = big;
    unit.setPixelSize(18);
    QFontMetricsF fmBig(big), fmUnit(unit);

    if (!haveFreq) {
        p.setFont(big);
        p.setPen(QColor("#e6e8ea"));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("---"));
        digitRects.clear();
        return;
    }

    // Lay out the digits left to right, remembering each digit's place value.
    const QString d = QString::number(freq);
    const qreal digitW = fmBig.horizontalAdvance(QStringLiteral("0"));
    const qreal sepW = fmBig.horizontalAdvance(groupSep);
    const QString unitText = QStringLiteral(" MHz");
    qreal total = d.size() * digitW + ((d.size() - 1) / 3) * sepW
                  + fmUnit.horizontalAdvance(unitText);

    qreal x = (width() - total) / 2.0;
    const qreal baseline = (height() + fmBig.ascent() - fmBig.descent()) / 2.0;

    digitRects.fill(QRectF(), d.size());
    p.setFont(big);
    for (int i = 0; i < d.size(); i++) {
        int place = d.size() - 1 - i;   // 0 = 1 Hz
        quint64 placeHz = 1;
        for (int k = 0; k < place; k++) placeHz *= 10;
        bool selected = (placeHz == step);
        p.setPen(selected ? QColor("#ffd24a") : QColor("#e6e8ea"));
        p.drawText(QPointF(x, baseline), QString(d.at(i)));
        if (selected) {
            p.fillRect(QRectF(x + 1, baseline + 3, digitW - 2, 3), QColor("#ffd24a"));
        }
        // Generous vertical hit area: the whole widget height.
        digitRects[place] = QRectF(x, 0, digitW, height());
        x += digitW;
        if (place > 0 && place % 3 == 0) {
            p.setPen(QColor("#e6e8ea"));
            p.drawText(QPointF(x, baseline), QString(groupSep));
            // Let a tap on the separator count for the digit to its left.
            digitRects[place].setRight(x + sepW / 2.0);
            x += sepW;
        }
    }
    p.setFont(unit);
    p.setPen(QColor("#e6e8ea"));
    p.drawText(QPointF(x, baseline), unitText);
}

void phoneFreqDisplay::mousePressEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    for (int place = 0; place < digitRects.size(); place++) {
        if (digitRects[place].contains(pos)) {
            quint64 hz = 1;
            for (int k = 0; k < place; k++) hz *= 10;
            step = hz;
            update();
            emit stepSelected(hz);
            return;
        }
    }
}
