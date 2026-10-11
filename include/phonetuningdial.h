#ifndef PHONETUNINGDIAL_H
#define PHONETUNINGDIAL_H

// Large round tuning knob for the phone 送受信 tab. Unlike QDial, touching the
// knob does not jump the value to the touch angle: only the rotation of the
// finger around the centre counts, so a fresh touch never retunes the rig.
// Every `degreesPerStep` of rotation emits stepped(+1/-1) (clockwise = +).

#include <QWidget>
#include <QString>

class phoneTuningDial : public QWidget
{
    Q_OBJECT
public:
    explicit phoneTuningDial(QWidget *parent = nullptr);

    // Text drawn in the middle of the knob (the current step size).
    void setCenterText(const QString &text);

    QSize sizeHint() const override { return QSize(220, 220); }
    QSize minimumSizeHint() const override { return QSize(120, 120); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }

signals:
    void stepped(int steps);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    double angleAt(const QPointF &p) const; // degrees, clockwise from 12 o'clock
    double degreesPerStep = 7.2;            // 50 steps per revolution
    double lastAngle = 0.0;
    double accum = 0.0;      // rotation not yet turned into a step
    double rotation = 0.0;   // knob dimple position, for drawing
    bool dragging = false;
    QString centerText;
};

#endif // PHONETUNINGDIAL_H
