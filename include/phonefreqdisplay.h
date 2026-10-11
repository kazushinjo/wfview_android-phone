#ifndef PHONEFREQDISPLAY_H
#define PHONEFREQDISPLAY_H

// Frequency readout for the phone 送受信 tab. Tapping a digit selects that
// digit's place value (1 Hz, 10 Hz, ... ) as the tuning step; the selected
// digit is underlined.

#include <QWidget>
#include <QVector>
#include <QRectF>

class phoneFreqDisplay : public QWidget
{
    Q_OBJECT
public:
    explicit phoneFreqDisplay(QWidget *parent = nullptr);

    void setFrequency(quint64 hz);
    void setStep(quint64 hz);
    void setGroupSeparator(QChar sep);
    void clear();

    QSize sizeHint() const override { return QSize(380, 48); }

signals:
    void stepSelected(quint64 hz);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;

private:
    quint64 freq = 0;
    bool haveFreq = false;
    quint64 step = 0;
    QChar groupSep = QChar(',');
    // Hit areas of the digits, index 0 = 1 Hz place.
    QVector<QRectF> digitRects;
};

#endif // PHONEFREQDISPLAY_H
