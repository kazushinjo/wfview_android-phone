// Phone layout for the Android build, ported from the iPhone version
// (wfview-iphone): the scope, the operating controls and a compact connection
// page as tabs, each filling one landscape phone screen, with the function
// button row along the bottom. Sizes are in iPhone points; main.cpp sets Qt's
// scale so the phone's short side is 393 logical pixels.

#include "wfmain.h"
#include "ui_wfmain.h"
#include "logcategories.h"
#include "phonerecorder.h"

#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHostInfo>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QSize>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

void wfmain::phoneSetupUi()
    // The .ui is a single large desktop window that does not fit an iPhone.
    // Shrink the base font so labels fit their controls, then reorganize the
    // three top-level sections into tabs so each fills one phone screen with no
    // scrolling: "スコープ" (the FFT scope + VFO), "操作" (all the controls),
    // and the row of function buttons pinned along the bottom.
    {
        QFont apFont = qApp->font();
        apFont.setPointSizeF(10.0);
        qApp->setFont(apFont);
        // Android also keeps a per-class default font (larger) for buttons,
        // labels, combos and so on, which wins over the parent's font in
        // every popup. Set those to the same size so the popups' text fits
        // the button widths computed for it.
        for (const char *cls : { "QPushButton", "QToolButton", "QCheckBox", "QRadioButton",
                                 "QLabel", "QComboBox", "QLineEdit", "QTextEdit",
                                 "QPlainTextEdit", "QAbstractSpinBox", "QGroupBox",
                                 "QTabBar", "QHeaderView", "QAbstractItemView", "QMenu" })
            qApp->setFont(apFont, cls);

        // The control section is dense; give it a smaller font than the rest so
        // its labels/buttons fit. Applied to every descendant to override the
        // explicit point sizes baked into the .ui.
        {
            QFont mg = ui->mainGroup->font();
            mg.setPointSizeF(8.0);
            ui->mainGroup->setFont(mg);
            const QList<QWidget*> kids = ui->mainGroup->findChildren<QWidget*>();
            for(QWidget *k : kids)
                k->setFont(mg);
        }

        // Scope tab: nudge the labels/buttons a little smaller than the base
        // font (the big frequency readout is painted, so it is unaffected).
        {
            QFont sg = ui->scopeVFOGroup->font();
            sg.setPointSizeF(9.0);
            ui->scopeVFOGroup->setFont(sg);
            const QList<QWidget*> kids = ui->scopeVFOGroup->findChildren<QWidget*>();
            for(QWidget *k : kids)
                k->setFont(sg);
        }

        phoneTabs = new QTabWidget();
        // Vertical policy Ignored: the tab widget must NOT push its own preferred/
        // minimum height onto the central layout. Otherwise, once the tall 操作
        // tab is shown, the QStackedWidget adopts its large size and shoves the
        // shared bottom button row (lowerButtonsFrame) off-screen — the cause of
        // "borders/bottom clipped only after switching tabs". With Ignored, the
        // tabs simply fill whatever height remains after the bottom row.
        phoneTabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
        phoneTabs->addTab(ui->scopeVFOGroup, QStringLiteral("スコープ"));
        // Placed directly in the tab (no scroll area) so the expanding spacers
        // auto-fit everything to one screen. The added frequency line is offset
        // by the smaller meters below.
        phoneTabs->addTab(ui->mainGroup, QStringLiteral("操作"));
        // Connection settings (profiles + host/port/user/pass) on the left and
        // the phone-wide RX/TX audio delay plus the save/connect buttons on
        // the right, side by side in one 接続 tab that fits without scrolling.
        {
            QWidget *connPage = new QWidget();
            QHBoxLayout *connLayout = new QHBoxLayout(connPage);
            connLayout->setContentsMargins(0, 0, 0, 0);
            connLayout->setSpacing(0);
            connLayout->addWidget(createPhoneConnect1Tab(), 3, Qt::AlignTop);

            QWidget *rightCol = new QWidget();
            QVBoxLayout *rightLayout = new QVBoxLayout(rightCol);
            rightLayout->setContentsMargins(0, 0, 0, 6);
            rightLayout->setSpacing(6);
            rightLayout->addWidget(createPhoneConnect2Tab());
            rightLayout->addWidget(phoneConnectButtons);
            connLayout->addWidget(rightCol, 2, Qt::AlignTop);

            QFont cf = connPage->font();
            cf.setPointSizeF(cf.pointSizeF() * 1.15);
            connPage->setFont(cf);

            phoneConn1TabIndex = phoneTabs->addTab(connPage, QStringLiteral("接続"));
            phoneConn2TabIndex = phoneConn1TabIndex;
        }
        {
            QFont tbf = phoneTabs->tabBar()->font();
            tbf.setPointSizeF(12.0);
            phoneTabs->tabBar()->setFont(tbf);
        }
        // Switching tabs could leave the previous page's geometry in place,
        // clipping the group-box borders and bottom row. Force the shown page to
        // refit the tab area and repaint on every change.
        connect(phoneTabs, &QTabWidget::currentChanged, this, [this](int){
            if(QWidget* pg = phoneTabs->currentWidget()){
                if(pg->layout())
                    pg->layout()->activate();
                pg->updateGeometry();
                pg->update();
            }
        });

        // Smaller font for the bottom function-button row so labels (incl.
        // "Disconnect from Radio") fit.
        {
            QFont lf = ui->lowerButtonsFrame->font();
            lf.setPointSizeF(14.0);
            ui->lowerButtonsFrame->setFont(lf);
            const QList<QWidget*> lkids = ui->lowerButtonsFrame->findChildren<QWidget*>();
            for(QWidget *k : lkids)
                k->setFont(lf);
        }

        ui->exitBtn->setText(QStringLiteral("終了"));
        // Require a second tap within 2s to actually exit, so a stray touch
        // near the bottom button row doesn't quit the app. Only the button
        // itself is gated -- closeEvent() and the first-run "exit" path
        // still call on_exitBtn_clicked() directly and are unaffected.
        {
            ui->exitBtn->disconnect();
            QTimer* exitConfirmTimer = new QTimer(this);
            exitConfirmTimer->setSingleShot(true);
            exitConfirmTimer->setInterval(2000);
            connect(ui->exitBtn, &QPushButton::clicked, this, [this, exitConfirmTimer](){
                if(exitConfirmTimer->isActive()){
                    exitConfirmTimer->stop();
                    on_exitBtn_clicked();
                } else {
                    exitConfirmTimer->start();
                }
            });
        }
        // Hide function buttons that aren't useful on the phone:
        // Save, Status, Rig Edit, Log (the iPhone port has no log window).
        ui->showLogBtn->setVisible(false);
        ui->saveSettingsBtn->setVisible(false);
        ui->radioStatusBtn->setVisible(false);
        ui->rigCreatorBtn->setVisible(false);
        // The round tuning dial isn't useful on a touch screen; hide it.
        ui->freqDial->setVisible(false);
        // Rig power on/off buttons not needed on the phone.
        ui->rigPowerOnBtn->setVisible(false);
        ui->rigPowerOffBtn->setVisible(false);
        // RIT and the tuning-step combo are dropped here (the scope tab already
        // has a step selector next to the ▲▼ buttons).
        ui->ritTuneDial->setVisible(false);
        ui->ritEnableChk->setVisible(false);
        ui->tuningStepCombo->setVisible(false);
        // CW / Repeater-Split / Memories not needed on the phone.
        ui->cwButton->setVisible(false);
        ui->rptSetupBtn->setVisible(false);
        ui->memoriesBtn->setVisible(false);
        // None of horizontalLayout_2's columns had an explicit stretch factor,
        // so with no stretch anywhere Qt falls back to growing every column
        // that has an unbounded-width child (the frequency label in
        // metersVerticalLayout; the Preamp/Att box in rightControlsLayout)
        // far past what its visible content needs, instead of giving that
        // space to the trailing spacer. That inflated both columns and
        // showed up as big blank gaps flanking the slider column. Pin every
        // column to its natural size and let only the trailing spacer grow.
        ui->horizontalLayout_2->setStretchFactor(ui->metersVerticalLayout, 0);
        ui->horizontalLayout_2->setStretchFactor(ui->tuningLayout, 0);
        ui->horizontalLayout_2->setStretchFactor(ui->controlsLayout, 0);
        ui->horizontalLayout_2->setStretchFactor(ui->buttonsVerticalLayout, 0);
        ui->horizontalLayout_2->setStretchFactor(ui->rightControlsLayout, 0);
        for(int i=0;i<ui->horizontalLayout_2->count();++i){
            if(ui->horizontalLayout_2->itemAt(i)->spacerItem())
                ui->horizontalLayout_2->setStretch(i, 1);
        }
        // rightControlsLayout (Preamp/Att + Antenna) has no expanding content, so
        // the surrounding row's default cross-axis distribution vertically centres
        // it, leaving a large empty gap above the Preamp/Att box. Pin it to the
        // top of its column instead, then nudge it back down by a small, fixed
        // amount (net ~30px up from the original centred position) so it isn't
        // flush against the top edge.
        ui->horizontalLayout_2->setAlignment(ui->rightControlsLayout, Qt::AlignTop);
        ui->rightControlsLayout->insertSpacing(0, 28);
        // Shift the whole column (Preamp/Att + Antenna) 20px to the right.
        {
            QMargins rclMargins = ui->rightControlsLayout->contentsMargins();
            rclMargins.setLeft(rclMargins.left() + 10);
            ui->rightControlsLayout->setContentsMargins(rclMargins);
        }
        // Antenna-tuner (check) + TUNER on one row directly above the Preamp/Att
        // box, and keep that box from stretching vertically.
        ui->preampAttGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
        // Narrow the Preamp/Att box by 10px, right-aligned so its right edge
        // stays flush with the TUNE button's right edge above it. Done by
        // wrapping the box in its own row with leading (left-side) spacing,
        // rather than measuring the rendered width at runtime -- deferred
        // width() reads raced against the first real layout pass and were
        // unreliable (sometimes shrinking the box to nothing, sometimes not
        // shrinking it at all).
        int preampIdx = ui->rightControlsLayout->indexOf(ui->preampAttGroup);
        ui->rightControlsLayout->removeWidget(ui->preampAttGroup);
        QHBoxLayout* preampRow = new QHBoxLayout();
        preampRow->setContentsMargins(0,0,0,0);
        preampRow->addSpacing(10);
        preampRow->addWidget(ui->preampAttGroup);
        ui->rightControlsLayout->insertLayout(preampIdx, preampRow);
        {
            // TUNER button, sized the same as the 送信 (transmit) button.
            {
                QFont tnf = ui->tuneNowBtn->font();
                tnf.setPointSizeF(13.0);
                tnf.setBold(true);
                ui->tuneNowBtn->setFont(tnf);
                ui->tuneNowBtn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
                ui->tuneNowBtn->setText(QStringLiteral("TUNE"));
                ui->tuneNowBtn->setStyleSheet("QPushButton{background:#bfe6f5;"
                                              "color:#0a3a4a;border:1px solid #7fbdd6;"
                                              "border-radius:8px;padding:2px 8px;"
                                              "min-height:28px;max-height:28px;}");
                ui->tuneNowBtn->setFixedWidth(80);
                ui->tuneNowBtn->setFixedHeight(28);
            }
            // TUNER right-aligned in its row (no trailing stretch) so its right
            // edge lines up with the 送信 button's right edge below it.
            QHBoxLayout* atRow = new QHBoxLayout();
            // Negative left margin shifts Enable ATU 20px further left than
            // rightControlsLayout's own left edge (10px offset - 20px = -10px).
            atRow->setContentsMargins(-10,0,0,0);
            atRow->addWidget(ui->tuneEnableChk);
            atRow->addStretch(1);
            atRow->addWidget(ui->tuneNowBtn, 0);
            ui->rightControlsLayout->insertLayout(ui->rightControlsLayout->indexOf(preampRow), atRow);
        }
        // Make the meters a little shorter so the F Lock / Transmit / Other
        // Controls group below them sits a little higher.
        ui->meterSPoWidget->setMaximumHeight(24);
        ui->meter2Widget->setMaximumHeight(24);
        ui->meter3Widget->setMaximumHeight(24);
        // The .ui gives the S/Po and 2nd meters a 280px minimum width (a
        // desktop-sized floor), but the 3rd meter has no minimum. On the
        // narrow phone column that floor forces the S/SWR bars wider than
        // the available space while the 3rd meter shrinks to fit, so their
        // lengths no longer match. Give all three the same explicit floor
        // so they size identically.
        ui->meterSPoWidget->setMinimumWidth(215);
        ui->meterSPoWidget->setMaximumWidth(215);
        ui->meter2Widget->setMinimumWidth(215);
        ui->meter2Widget->setMaximumWidth(215);
        ui->meter3Widget->setMinimumWidth(215);
        ui->meter3Widget->setMaximumWidth(215);
        // Frequency mirror above the S-meter, full width. Normal (non
        // 7-segment) bold font, large.
        phoneOpFreqLabel = new QLabel(QStringLiteral("---"));
        {
            QFont of = phoneOpFreqLabel->font();
            of.setBold(true);
            of.setPixelSize(22);
            phoneOpFreqLabel->setFont(of);
        }
        phoneOpFreqLabel->setAlignment(Qt::AlignCenter);
        phoneOpFreqLabel->setTextFormat(Qt::RichText);
        phoneOpFreqLabel->setFixedHeight(24);
        phoneOpFreqLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // Column order: frequency (top), gap, meters, Other.
        ui->metersVerticalLayout->insertWidget(0, phoneOpFreqLabel);
        ui->metersVerticalLayout->insertSpacing(1, 2);
        // F Lock and the transmit button, sized to be easy to tap, placed
        // below the Preamp/Att group in the right-hand column.
        {
            QFont tf = ui->transmitBtn->font();
            tf.setPointSizeF(13.0);
            tf.setBold(true);
            ui->transmitBtn->setFont(tf);
            ui->transmitBtn->setText(QStringLiteral("送信"));
            ui->transmitBtn->setStyleSheet("QPushButton{background:#c8ecc8;"
                                           "color:#0a3a0a;font-weight:bold;"
                                           "border-radius:8px;padding:2px 8px;"
                                           "min-height:28px;max-height:28px;}");
            ui->transmitBtn->setFixedWidth(80);
            ui->transmitBtn->setFixedHeight(28);
            // Mirrors atRow's layout: same leading offset so 周波数ロック lines
            // up with アンテナチューナ above it, then a stretch so 送信's right
            // edge still lines up with the TUNE button's right edge.
            QHBoxLayout *txRow = new QHBoxLayout();
            txRow->setContentsMargins(-10,0,0,0);
            txRow->addWidget(ui->tuneLockChk, 0);
            txRow->addStretch(1);
            txRow->addWidget(ui->transmitBtn, 0);
            int txRowIdx = ui->rightControlsLayout->indexOf(preampRow) + 1;
            ui->rightControlsLayout->insertSpacing(txRowIdx, 20);
            ui->rightControlsLayout->insertLayout(txRowIdx + 1, txRow);
        }
        ui->metersVerticalLayout->addWidget(ui->OtherControlsGrp);
        // RX recorder + rig power ON/OFF on one row, matching fonts.
        {
            // Record: a "● REC" icon. Light-blue while idle, red while recording.
            QPixmap pxRecIdle, pxRecOn;
            pxRecIdle.loadFromData(QByteArray::fromBase64(
                "iVBORw0KGgoAAAANSUhEUgAAAQQAAABgCAYAAAD7GgzyAAAhaUlEQVR4nO2deZxkVZXnf+fe9+LFlpmRtYAI7dYCn6nEpaVEPyqfzrQtQaAW0IhWu+lpHQZsbdoFasMlMlyohVYc6ZlpoEdaaYWJUKEWik0707Fr3KD9qFSBoKj0yFZLZmQsb733zB8vIiurMrIql8it6n0/FVWVkW+57737zr1nuecAEREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREREYsGmt/TMzEf/Q0BABG32joiImJ2mUOBwJRnEAYhAAC90AUi3WrLPLPoKYH2LQftP1DiUjarIyERETH7zLpAyOdZoBei0EfBsb/LFovyDWdkO52DwwwA7rIMuRLOzW8h+9hti0WWJQClHDQQCYeIiNlg1gRCtsiymIWmxsieH2DDU+45UvEFEHid8v3XaqXOkIbxEq1U2BghwVpVhGH8Gky/Z+DnRPihuSz5WGEl1ZvHLhZZ5iLBEBHRdtouEPJ5Fv394KYg2Pxd97WkgitZ6UsBPsdKdkgQoAMNrQJo5R/dICEgDQtCChABnl0Hs35GGOb3iPiuJw7H/7WUIwWEQqf5/4gIAAAz5ftB+3tKBGRHv16RBfcDTEQAEA0kE9BWgRCO3OELuvkBe5UgXs+s324lUtJ3XQSeA2ZWAJgIxKH5cHSYJwBMYAKYmRhgECClaZEZTyDwXbBWv2Dm/2aO/O5fCrnzvDyz6McRATRfZItFuWJ5dlZmXD0HwCUAK/aBC/3gmdpTmmpcm5o3dQahC4XW9qMpw0zZEsSK5aBCL9Rk7k2xyHLf8kHq7+1V891vFhpt6cBjZwXX7Tp0XtyIbxdm7F1EBM+ugpkDEASBaDrnZECDWYNZmokUGWYMvmM/xiQ23HhR/H7g1JotZIssVywH9ffilO3QzEwlQOTo6Gee33Ow0xXGadrHEsk6ockkJu1BirJkfnHLJZ0Hxm5fLLLctw/cNgG1yJmxQMgWi7KUy4Wzgvsq/WTIzYZpxdzaiAaBCSTacZ4mzKwBZtNKSSEFlO/dPVwf+tv/fsVZh4rM8tgOMtvk8ywKBdIb95RXxxOd5zu1IQ2maY++RMQgaGgETOxAowKSh4TAi1qYf6hz/LlbLiG3uf3YWdkJ28osCkR6/c6hNyQ7MmucWlmDee5mCsQ63tEtvGrl4S2Xdu5ttmdqB2EqFiGa15wvvpB2U/FeAq/SzBcQ41UQIiOEEZOGARBBK4XAc5iIRkiI3wH0M4b6LkE81BQQkV0qxJjJzs1R+WPffvGMRCp1RzyZvMgeGYHruYqIZLsaORYiEgDBd+uawUh2dr83Q3T+9TsOfSBHtDc/wEYrj8bsMSgAaFb6fakuvA+cgTBmKP8aXZK5+dHQvgf2PSct/Gc/eX/1Z5pxv1MN7snl6DAz06TiN0KXr4agNyc7kQe6IGblKbVGB4xUF+CWAwawd7Q9k6QpfHM5qPyeg2f5MvU3vlLvN2PWK6RhQAUBdOCFtqnAY6U8btxLIhIkpOyShvk6acZex8x/7Tn2i598oH43tPeV3CX0m7HnmJUbsAiYtkBovnjXffvAGxMd6R3StM6ol4cDAuRsCYOxEJEgEOzyUGDGk2fHk+nBDbsOfbjQR7fPvVAAiES5PoLAqZUDzFDQHn3gUM9iEAkScSHNVxkx61VE9G4S9mdvuK+yhYj+AQDATJPRoYnYrpURuLVhBWAORQICkhkDgmpT3TFbZFnIkcrnBwz/gvPXB9K4PhaPL/FtG55d0QBpIiJmFqFiSkQAHZmbataBhgp8hlPXIJCU5mlmIvl3vo0P3LCnuu2pO/ZsLRRIjZ31nmpMq+NmiyybwiDekX4QJLqdynBAQrTvRZgkJIThe7YSJGQ81XXbhl1DKPTR7XOvPrBE434SUdvvAwFgrTjwFQe+w2CwNGMvtTrSt2zeM/K2WPWZv0J/f1BgPvFMgSGIYITCZvaF9+hpw7BUg3hqBs18fsAo5ChY/60Xzw7SqTusRPKtbq0Ke2Q4tE2REEB4zIYXoQVEIDT+hNuqwGd7ZFgJITviHR2fP/uDl/7Z+r848Jc3rVv+7PTUmcXPlPXHPLMo5Uht+M7BC+Id6QeJ0e07NTUfwqAJgaTWCr5TV/FUx20bdh36rzkiVWSey9Fv9iEiAgkCSSIyVODpennYT3R2/LmTPOsbhUJBZ0vz6D2YBYpFloVCX7D+nhcvjKU7/s2Ixd9aLw8FSgVMREbDRjUtKMRgrdguD/tmPNlnWsnvb/rW868qEOl8fg7tKwuEKUpqFgWAN++onG4k4ruIRLfv2YpoLjXR1hAJ0ioQoVDI3LZhx8FVOSKVLS4kocBqKh9m1szHrvY4AoEEEZm1oWE/1dX1ng07D11fyrXnmhvG2ym190QfAhQDSvPk4gCaBtONOw+/LZbs2EOE05zaSEBCGNRqKsDMjfsWNO8fAMWMgJkDnsheQUQgMu3KUGBa8VdTMr0n/8Dwkv5+MDPP83qfuWVKo/r+nnDmylLfGYt3nDZfasJEhEJBkfIcNmLxOzfvqLxuy1p6YaEYimKJtDzhgMaNv5jBzNDKR+B7umFFmGhnw65UtTRi/Rt3Hi5uW4P/mNk1M2LxpCDZ3kerVSDjScArk3WibfPMIkekPrWrfDab5r0A0o3Bp2WjmLUSwpCxREqSENBaA1oDRBBCAgT4jo0gcFWoMo0XKEIIw6mV/WRn97l2eeifiOiKhnA9ZewJk37iTWm9YefhfCLTvapeXljCoAkRicB3VTydOd1V5TvzzBfvL83zqk4isNbaq9d+xNAuWhrXBYg0heo9S2aYBLIY/NJYInUaQPDsim4lFIiItApUsjOTqo+UrwVoPXpZojB5C/4ozCykQZ5j/wLQhwgCPI3DtIJIKKcekyD8GgD2H5hgpsBM+0uga/ewpfTIXaZpLXXrw4pIjpv5MGsmEBId3dKtVWy3XhkE4wca/JQgGtZamVLIlzHRGwl0USKVOcu1q9Aq0KHHalwbzfrIcJDo6L58866h92xZTd86lWJcJvVCj0rr+8vnsjA/aVdGNObWOj0liIR0quUg2ZVZZe8eurKUW/K1+YhRABBG0AhBILierq390pozD05l9027h7t9134rkdhspbre4tZGWnZkEITnOAzmbH6AP13oIwdgmrJfnaANKyn92sgnblqz5HtT2neKTPSSZUsQpRypjTsOfTreveT8+vBQQGL8dIW1ZmnGiISEZ9f+Z6D55ptWZ56a4HS3bnyYuxyn+l8EUcG0EmnfrU8oYIPAZQ3+/LV7eNct74KHhl13Rhe8CJjSCO8H6itWKm0GYZzBwja4EAnXrmuSxtZND/PuLBD66+cxss9UyXSe+TAGIdA7wbDbH/7TDFHeellmCMDubJHvP5urd1vJjvd4dkXhGO8AgUSoKsVe7tfr5wF4JM+gAk2nEzMERDLPLF76KOSz57d3ytx/nFDzfJ5FIQv9yR1Dr2TT2OBUq5rEeE8Ia80yFicCyqy8K2+8pGsXEA5eGIRohnsDwIrlg9RzoJdzq6gM4Esbdx3+Hgm617SSr/Bdu5WAlb5rq3i661yqldeAMqX8wIBR6OubU1f2fHBCgZAtsiwQqc27h/rMRPqdXr0ya0FH7YQAoTwvSGYyL6kPH76WaGl/w+swf1M/ybphvUah7wT6faHxLzNd/eijxm0ryd9YPHwViC8UMnaaCjw+Vg9mImVYScOpjZwH4JGpBv4chQjzVRSLTNesbK/9pXCc3/X0gECkg91D65OpdKI+MhyMc+MyszBNJqCuXfvSLWuX7s0PsIHB0RwbrdvLTLc+CuOalfTzzTsPvIvM5A+lGetU/vh7CSZuGHU/AKCE3t55t0HNBScc5VdkwxFGa/50M0RmESHdao2FMP/2EztHluUAveisxkR828qVfn6AjW25JWWtgl2xRJLQSrAxQwiAwGfOfUPbADPlcqSu+3blNIJ4v1uvMrVQTZmgY1ZS+F792i1rl+699RE2C30UnNCISsTXrCT/1kfY3LJm+RPKta8zrYQggg5dFKybHgkCSx0Ewown37X+3gPnnipuyONeYDM441P3V3pkzLrQq1f52KnqQoaISAWettIdSy2Jy0HE/YML1/ZxQpiJifafyETKzIm5aVB7yTeeTcxSl8fTnV0q8PWxkUYMVlYiLZ1aee/2NcvuyA+wcc1K8lsfsTXXnI8gW2T5tLf0a06tvM+wEgADZiwhEukuI9GRMYRhUOC7vwdjlzBlDMCoOncyc3yJ10h3Fij/A1YyaTSWLi8qiAgqCFir4KrGst/FO/UjYoJQJ5ykETlz0p52Mxg+G820jrVmtIo6ZCYiAUH0JQAEDE79PES8YjkoNGrS3amMJZnI1b6/37Urd3q1yt8II/aG5d2d/+nz77TWbLt0yS8B4FSIXJzQhkAACr1Q+SLHHB66zHdcgBZjFBxJ366zMGOvdy8YefVW6npyocQlTAdm/qMJo3NBYA2A+Pk5bFJ7YKYCkc4/MLzE9dSbfNcmYoixsyFmZsOMC6c28nw87T0EgAu9vdMapPp7oQoADMXfcEbU0ybHf/qruvX0sZ6PYzN/nexMKBA+kw/VBfc+5xVCWK8MPIcxgzDR+YRZ63iiI2ZXy28B8CR6Iablo58nmJn6B0d/uijwvAmEs5ZhEhqxHziOn38yaIg8sxh6FCJ/nGjJKdEPTCSIsyWIEqA8LV5jxOPdvltnInGM6CNlWHFDBd7eQt/p1WyRZWmaruTmC/6Ftd2/BfDb5vdh8hQQeqELAJeI1OIyOs2Mib0MjZdGsPuWWKIjZle8ReFdmIBmh+4F8M89vfNjGQ2YZLbI8rkhiGyxtXGzmfRr3/KjLAWq0EfBxp2HP2KlOl4TxiIc7T9nQBtmnHzPeS5he78AgFJ2ukKPoKHrx7XYt5kVjevlIHidkeyA79oK4/onN00KPwVAK5bPPOCsIWxlmMUJPNncEicrEwqE5kujOHhj47Yv5imTUEEAAr326kfYzALz4k/WLpUbU9IJO12p1ZdFjt1wf+VDROKLvlPXE8Tx61giYajAvbeQO71aLE4zEIshArcOQfSljbsOtylSUWjTiovArT+9dfWSqxpftnRZMdG5Ex2FCKSVBlg/DoB7ZjIDGj0mMeapPyxEJhQI+5oPi+hcrZqLRxctpHwP0PoVZ7x4KEG0bGROg5TCGbcwE+qdm3dXXoAOBIRx1FumKSABkpopxqwTBHQS0XIQvcrRw38atzrPde0qWI8zvAPMLA2DXLvmGixuBpj2Zaf5shCR1gFi8eRr27WWgbWGlTBQ9ZyXT7TNEfWGXsaaj8pkcORAEIFnQynxLABM+xojJmSChSJMAHik+EwCoDO08sE8vh8uFpq5BETMituBOhPASH//HIWihkk7AJBlxTvuamk5H9NOEIXSl0RosWEg8Fw4tbJurG4ctx8T+fF0OlYfHvr0jauXPNUMJptBo+E5dU3TinJsAUODEwKMkYk2aao3RLyMtWolDZiEIBX4viQaBnBKuAHnmom9DEScf2A44Qb8R9r3gHkv+zYDiEhrra14PMGqchaAxxsrN+cUz63p471i4Ywl3KCx6LExSxtNAnIUjeW9Ot2diVWHhu/atnrJlnYtxGlnaDqHaypEI4lMqy0IRMzMtOm+4bRmPX4AYgBCgKA9xWajRkd/u5oY0eC4D90NwGD4xxvVFhPMGkLLKQWxtJPGCD/hB4AMA79INpOgjEsCMrrmXyvTSoh4OmPURqq3Pm0/fGU+z2L6hsT555rbHjUIsMDccvgJZ1DQWukAAPr7+yOVoc2cWEk8OWTBERbcgu1JEqbqYGkYwrCSkkgg8Own3Fr5s1svy9wFZkIWbSyUy4onmchkEmhmFsAEeS4ZAAFndMfJOWF6NULMOOl65YLhuK+Ha4Bi/qJ9hcZBRFBKzdv1hJl8xkINYy0LHHcaxhDSICMWJ9+zh5Xn7AXorvrIwe/cnHuZHYaYg9uWQpwZsVSnlIYYfVlndDgFxJJA+UV7WcsNmsdffkBT7cwJLf6hGsWGa3kxAJgzO9ApxMQvBzMZOw76ZBjDJEQnqB1dY/4gEKnA9wWZw8CRRVtz2YJER5fRNBQCAGtAqQCBW4dmrVolPGWwNsy4UIE36NdGPsVJ44kvrEodav5+5gbEY0/ILGMWefXqN5n1UwRBDD2je0XMrIIUCdALE2wBACj09QUbdw3bRC0EUWgZBhNiRmCmAYQmhOMtnYyYMi0FAhFxnlkU1i2vbNw19FtpWi9TvrtoJTEzszRMUp5btwL+NRCuyZ+rvkRE0FoHdnXoH8BUBgkhmBMQOF0znwvg/HiqSzrV4XEBRwQSyndZGrE3wjD/qlp9YSMAXLuHrVsugdf2TD4ELU1Lai/4x62ru3/Q1mMfzdj+xKPh5IQyCYHxHg4i1pqNWFz6fr0bAPaXSm0ZoKLqTUeYODDpiHx+RggCh+l3FicEFtIgFfgvjPgVF3O5BJqZIQSRgKd89Zmb1i2vHLvJ5l2HzvOc+qfiqa4/d+2KArM8ZjJGSvmpZGfn1cynr7ru3oPv++Il9OOw/sQsBNUwIwB35wfYWGJDHk60K4fEICZKMtIz6vXh54SQLaejTNCGaUntu2cBwIp9y9vyHEejE5kpPwiJXui2qmCLiIkDkwYbRl3mR4joysWsMBDAMhaD79Yfuzn3MjtbZElzHaLKIGmYy/MDbOMABJZD7z8AbiyceQzAezftHn7cSnb0u/XK+GIvzKgNDQWxROqViWTqe9d/54UrCn300FRKuU0FQWG4dLHI/NFLZv9ejQnVfqqhVvF4iQAWkqCZVwAAentnrDLkBzjuVg6vBctHtxL9ujAmajGfHzB6eno5l4Vun7F2YXO8tQwaAIQU/9dzHdUqUcVigTksA06QPwCOxM3PeTu0Dgp9FIxdbUk4Ujm60EeFjbuGTkt0Zj5st8gUREIYnlNThmmlrFTnvRt2HHx7bi396ORIAjoIACDQL7XSrSMVG6HLBFoJYLSPTodmdSbPrp5jpbvv9uoVd/N9lZ8KQQ8y0UMHf5/8WeGaqeVZOBmY0MXT39DxXMN+PPCc56Vp0fFqBCxkCJC+42gh+N8AzKgjzQalXE7190IVmeXQs7/5mFMZ+YmVTBvMetxLTiRk4LuKiBIyZn3r+uILLylmsfiz+QyGKcoCgX936xVfkJDNmO8mxBC+64Ch3/zRe4YyBSI9XfVvxfIsAYDWwUUkSWnWpmnF32YmUp8D8OOlZ1V/uWn38K2bHxhZt+nh8lKEE81FOkeePBN2IiLiIrP84kVn1MD4VzORABZhfnoGa8NKkO/ZT1eo65fMTJ9dgIkuiIj3AXzbNSt9kHtl4HtVacbQSggTCek7dmDF02fKROwOIuKeeYi8bCeFAmlmptSPu37LrPcZsQSAY54TEWnlqXiqa2nSpHcCQH5wcHoz18EwnR4xv0/5vgSBPaeq7MpwoH0P0jTPtZIdV6c6O+5hW10EgIvFxZgPZGoc9wJLzaV3Gv+sPJdPtP2ChKENywJBfuOWS8jtH4RcqNOcApEuMsutl572ZOA7n4jFUxMmhSVBhl0dDpKdmYvX7zr40Vxu8Zeu6x+ELBRIk6DdRsxsKQwZYcSp1vzRMV9PSRiG5eFIb76/vMpMpP/Es2uamhGiRAaIEHi2cu1aUB0qV7S2BgEgu4ijQCfL8QVCjhQzU7wz8388x37CjCepET+/WGAiIT277ho+3wlgNE3XQiVHpPIDA8b21UtvtyuHdyfSXS1VByBUhdx6TZmxxI2b7iufkyNSeV7EqkNDlSNf3eXUqkGr/BsEkq5dVfF051s27Dj4nwt9fcHVj/Dkg82Yad/yQcoWixJMWwC0cHECDGIrmZKs+fs3rUs9m2cWp0LWpBN2nhIgCn0UEMQ2w4zRsXrdQoZZ63hHJwWe87+/cEXmN9nGyDDf7Tohg72amUki/iHPrh+WpkWMFoKYiFTgwzTjSdb8jwAw71WqZkCBSOeZxZZ1y/brIHjASnUScwu3KkN4rq3NeOqW6+49+KbbVpJ/9SNsniijdp5Z5AchC319wR8n3rElnux8g2dXx9W4AABmLZiZpMRXgaPc8Cc1J5SsOQoNVoflU3ejypvNePIc32lZ3GKhwUIa8Oy6FzONG8FMKxZJmGuhQLqnh+WNudQfNu46+PFEYunXbN8PWqVNIyLp1EaCREemb/3OQ1fdtIb+qS2uyNlIoTaGifz8TYHGRJ8LPOdSEkSNOvKjLyQRkfY9CCveYSWSezbtGX7f1pX00G0IIzdXLAc1C7VkEbo09x8ANyI69eb7Rz5lxpLrnXq5ZaFiZtYxK0lOdeTJhNN9H5hpXqp+zQOTmGoR7+9hUbrkHHfTzuG/E9J4MMxjv7BhrVWiq8uoDR3etn3N0l/NWym3aZLLkSoWWeZW09c37Bq6IpnOrLWrQ60rbRMJz6lr04htvX53dfe+n/a/OLNEsnOfQq1Js3r19tX0kw07D92R6lrywVp5KBDH1BElISjwHG2Y1hIhzPtveKD6ZYL48hcupv846nhj/r/xvsprhKBCLJ683KmNaMK4ALAm2ohZRuC5ny/kyCsyy9wiNKhPh0npXs2HtHUNPbRh5+FSsqs7a5eHFmSxVyBUFcx40nAqlacTxpJteWaRneOO3Q727QvXOH5y96GPuHbtQmlaGRV4mo5JdkuAUL6rEh3dS4OR4ZsKhcKV+YFeY3rFXmcjhdqYtpJUsURCOn590/aLMz9pJbhW7AtDmV1Rvt6uVf4slki+3LPrWohjwrpJiMB3mUiIeLrrE06tctWm+8rfhdJ7hZC/Ya2qGiJJEmcT6O2AfmfM6jCdWrmxbqRlJjplpToNuzr8w6frD3+zWde0bTdggTP56s9Z6H5mMfJg+UNevXq+GU++cqJimfMJM7MQhmZoVr73F4W1NLJYDUJHVIdlk1AdhHSqZWUlU3+54b7hrxf6Mg9PK2BpFlKojYW1hpU04A7R6cDYkOUjFAqks0WWpcsyQxt2HHwvDHNQGmZMK3/8Wo9GZma7MqSENDtjifQVRHSFVgrcyLIipARrDc+uwqmXWy4iAwBmzcIwKfA8R7C+upTLqWyxuKg9N1Nl0k+8seCJbr44c3jTvQffTwn5QyENaBW0SJc9fxAQJLs6zMqhQx/fvnbZj/IDbBRognX4i4CpqA7MTKw1BOOWjxef+ZP/h5IHDrMRTe2sbU6hdvSxFSglSfNxowBLDTdqjuhHG3YcfH8snvoWgYTSfssXmkhIVgE7tbIGg4lADXnA3PgZgDieMCBhqFg8bTjlQ9dsW7fssZMjAnRqTGl0H/WTr1v2Y9+2rzHjSSGkoZlntjy2XTBzkMxkzNpw+fbta5d9OVz8s3iFQZOm6uBDfcS1axN6HYhI+G5dJTq6zjWtxA2lXE5NN3AnNBqTbP8HkkBSTyJDZ+iCZWP72mXf8e3au4WUdsxKSdY6QCsDMRE1M001YgoExvw8Uc4JZq2EkLCSHYZdObx+27plX8/n2TjVhAEwjUCj0Ye0buntTq1ytRlPyoZQmD8dPbRC+8lMxqhXRm7felnm6jyzKPSeHIagQoF0qQRx8+plfwgC72MxKynArQ2GRCScelWZ8eT6DTsO9RT6+oLFPO0t9FGQH2Bj27pl99j2cJ/WwRPJrm4DYZqFADPwgITVnbWykp1SGKZn14av2r5m6d//6QAbhcLiH0imw7T0/+ZD2r66e1QoGKYlJgqgmU2YtQYByUyXaZdHbt96SdfV2SLL/rCqxxzOXEghXCkXMPPop/kzgMCIxafdnqbqcNOaJXfWq8M74ulOg5mdsecKP1DaDwJhmAaIvgwAK/Zljz4vQYcrnBGM3392P81z6im8yOGCsAHji+vO+PGB3z33Zq9W+4owTC/RkTFIGsTMihkBg3VDQIw/dhj2qMPUcBwwM8cSKRFPd8vAd/cqv/bW7Zd1/69skeX3T4JZ5XSZttUoFAoDRqGv+/YN9xx6xkwk7oinu89wqsMBANmymEhbYdaaVSyeNJh1UB+prt96WdeX88yiH3Nfi49ZdyU7YbDuMoQx5tI1QAbg226HF9gzGqmbqsMndh/6iGHXezuWZroCD60K7BlaMZae2f2OjTuH1hfW0E3FIst9o22lRKoLBlHGaOHEnFVYwbDSgO9TbCr7FQp9QaMaeRnARzfuOvxVj3Etkbg8ns4sIQCB70EHHrRWYEA3k7USSJCQJA2DpGFBGAKeXYdW/g8Dz/0fWy7p+BcgjGE4FdWEsczIjFzo6wuKRZa5y+nB9d/8/Uq9ZNlXk52Zi9x6HSrwZkkwMDOzJhIylckYnm0/qYPgg1sv7dzbzC1YmNPEFuEqPZLirloZTzr1YQ0+8ooSUVgRhtmHxUMA0N8PLkxjHX/onmNxc2HZH9bfc+Ddvpu60K2WFbdKb06kNacBwU7oOoPONzMMaP5RfQSF0AA3t6HOgkgrnRIqwONAKOQmu29zdWO2BLFtNf0cwFXr7619hri2ignvAOvXa61fDqDDjMUFCQHWGirwAWZb+f5BHfi/ElL+QMrY/Z9bFftp89gNYXNKCwOgTeGYYyPjbthT/iBIfM5KpV/q1mpQgadCaw/RTM7XnA4SSRlPd8Jz6p4QYrs//Ozfb8v9cflkMSBOhjmtOrVAyedZ7O9plnQPYWa6YWf1NMA/HYbRoQWZIvAVk6jLROygURs6UFhzZn3scWYrwcxipW2jdzOOnIj4um8/d1o8nfkwmD9kxhOnqyCA79TAzAqg0AXELI6UKhp/MA5dXtwoViJNK0FGzILn1FwwfZMD50tbVi99DBiV7vMaeNRMcnK8bQp9pNCm8OnJnA8Aeg6ML2Caz7NA7/yuXO3vhWqHUGNm6h8clD0HeidVqHX02nuh57vPLETaruePlbibdzx/ukh0r2EO/pqVvsBKpg0AUIEP5Td0vUZ6nBAGkSAhDRLSgDRjIEHwHRsA7Qd0SbO6e8vFnU+MnusUSm8VcSKYmIF+gMYu8lqRBaMfKPTPtaF58TErhj9mphJwVMjn5u86K4TmNzHrC8H6PFb8ambVGUt2yKbHkkggcG2AMMSM50nQ4wD9QEpz72uGzH9vCppskeWKKEtuRETbmVVPwKhgwPhRPD/AGbt26Awr3fUSp1qFAcCIp6HcWmX5S7t+febrUTk2hrzILPf1R4IgImK2mLOQ46bu1tBpNSahSzeXsu4/UOJSLqtPxbTYERFzybytQWBm6u8H9fSASkf9poRiNqupkQR+XhoXERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERERsdD5/x9PQ9cGkuC1AAAAAElFTkSuQmCC"));
            pxRecOn.loadFromData(QByteArray::fromBase64(
                "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"));
            QIcon icoRecIdle(pxRecIdle), icoRecOn(pxRecOn);
            QPushButton *recBtn = new QPushButton();
            recBtn->setCheckable(true);
            recBtn->setMinimumHeight(38);
            recBtn->setToolTip(QStringLiteral("送受信音声を録音(L=受信/R=送信)"));
            recBtn->setIcon(icoRecIdle);
            recBtn->setIconSize(QSize(98, 36));
            recBtn->setFlat(true);
            recBtn->setStyleSheet("QPushButton{background:transparent;border:none;}");
            connect(recBtn, &QPushButton::clicked, this, [=]() {
                QString path;
                bool rec = phoneRecorder::getInstance()->toggle(path);
                recBtn->setChecked(rec);
                recBtn->setIcon(rec ? icoRecOn : icoRecIdle);
                showStatusBarText((rec ? QStringLiteral("録音開始: ")
                                       : QStringLiteral("録音保存: ")) + path);
            });

            // Rig power toggle, shown as a power-symbol icon (red = ON).
            QPushButton *pwrBtn = new QPushButton();
            pwrBtn->setCheckable(true);
            pwrBtn->setChecked(true);   // assume the rig is on while connected
            pwrBtn->setMinimumHeight(38);
            pwrBtn->setToolTip(QStringLiteral("無線機 電源 ON/OFF"));
            QPixmap pxOn, pxOff;
            pxOn.loadFromData(QByteArray::fromBase64(
                "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"));
            pxOff.loadFromData(QByteArray::fromBase64(
                "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"));
            QIcon icoOn(pxOn), icoOff(pxOff);
            pwrBtn->setIcon(icoOn);
            pwrBtn->setIconSize(QSize(44, 44));
            // Icon only: no button background or border.
            pwrBtn->setFlat(true);
            pwrBtn->setStyleSheet("QPushButton{background:transparent;border:none;}");
            // The rig must be powered on before recording is allowed.
            recBtn->setEnabled(pwrBtn->isChecked());
            connect(pwrBtn, &QPushButton::toggled, this, [=](bool on) {
                if (on) powerRigOn(); else powerRigOff();
                pwrBtn->setIcon(on ? icoOn : icoOff);
                // Powering the rig off stops any in-progress recording.
                if (!on && recBtn->isChecked()) {
                    QString path;
                    phoneRecorder::getInstance()->toggle(path);
                    recBtn->setChecked(false);
                    recBtn->setIcon(icoRecIdle);
                    showStatusBarText(QStringLiteral("録音保存: ") + path);
                }
                recBtn->setEnabled(on);
            });

            QHBoxLayout *row = new QHBoxLayout();
            row->addWidget(recBtn, 1);
            row->addWidget(pwrBtn, 1);
            ui->metersVerticalLayout->addLayout(row);
        }
        // Drop the now-empty grid (frequency lock moved next to 送信, RIT and the
        // power buttons hidden) and tighten the column spacing so Other
        // Controls sits a little higher.
        ui->metersVerticalLayout->removeItem(ui->powerControlGrid);
        ui->metersVerticalLayout->setSpacing(2);
        // Remove the expanding spacer above the level sliders so they sit higher.
        ui->controlsLayout->removeItem(ui->verticalSpacer);
        // Shift the whole slider group (RF/AF/SQL/M-A/TX/Mon) 24px to the right
        // (was 80px; the sliders are now spaced further apart instead).
        {
            QMargins clMargins = ui->controlsLayout->contentsMargins();
            clMargins.setLeft(clMargins.left() + 24);
            ui->controlsLayout->setContentsMargins(clMargins);
        }
        // Add a "WF" column to the level-slider row: a vertical slider that sets
        // the waterfall colour floor at runtime, independent of the spectrum. It
        // mirrors the RF/AF/SQL columns (slider on top, label below).
        {
            QFont mg = ui->mainGroup->font();
            mg.setPointSizeF(8.0);
            QVBoxLayout *wfCol = new QVBoxLayout();
            wfCol->setSpacing(2);
            // Zero margins so this column is exactly as tall as the RF/AF/SQL
            // columns. Default layout margins (~9px top+bottom) made it taller,
            // pushing the 操作 tab's height up and clipping the shared bottom
            // frame/buttons once the tab was switched.
            wfCol->setContentsMargins(0, 0, 0, 0);
            phoneWfLevelSlider = new QSlider(Qt::Vertical);
            phoneWfLevelSlider->setRange(0, 160);
            phoneWfLevelSlider->setValue(prefs.mainWfFloor); // finalised after loadSettings()
            phoneWfLevelSlider->setToolTip(QStringLiteral("ウォーターフォールの色レベル(floor)"));
            // Match the RF/AF/SQL sliders exactly (Fixed, 120px).
            phoneWfLevelSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            phoneWfLevelSlider->setMinimumHeight(120);
            phoneWfLevelLabel = new QLabel(QStringLiteral("WF"));
            phoneWfLevelLabel->setAlignment(Qt::AlignHCenter);
            phoneWfLevelLabel->setMaximumHeight(15);
            phoneWfLevelLabel->setFont(mg);
            wfCol->addWidget(phoneWfLevelSlider, 0, Qt::AlignHCenter);
            wfCol->addWidget(phoneWfLevelLabel, 0);
            ui->levelsHorizontalLayout->addLayout(wfCol);
            connect(phoneWfLevelSlider, &QSlider::valueChanged, this, [this](int val){
                prefs.mainWfFloor = val;
                prefs.subWfFloor = val;
                prefs.settingsChanged = true;
                if(!receivers.isEmpty())
                    receivers.first()->setWfRange(val, prefs.mainPlotCeiling);
                if(phoneWfLevelLabel)
                    phoneWfLevelLabel->setText(QStringLiteral("WF\n%1").arg(val));
                // The phone layout has no Save button; persist immediately so the value is
                // applied on the next launch. Must match the "Interface" group
                // that saveSettings()/loadSettings() use, or it won't be read back.
                if(settings){
                    settings->beginGroup("Interface");
                    settings->setValue("MainWfFloor", val);
                    settings->setValue("SubWfFloor", val);
                    settings->endGroup();
                    settings->sync();
                }
            });
        }
        // Spread the level sliders (RF..WF) further apart, and use a larger
        // font for their labels and for the antenna tuner / preamp-attenuator /
        // frequency-lock controls to their right.
        {
            ui->levelsHorizontalLayout->setSpacing(8);
            QFont lf = ui->mainGroup->font();
            lf.setPointSizeF(10.0);
            QList<QLayout*> pending { ui->levelsHorizontalLayout };
            while(!pending.isEmpty())
            {
                QLayout *l = pending.takeFirst();
                for(int i = 0; i < l->count(); i++)
                {
                    QLayoutItem *it = l->itemAt(i);
                    if(QLabel *lbl = qobject_cast<QLabel*>(it->widget()))
                    {
                        // The .ui caps these labels' width for the 8pt font.
                        lbl->setFont(lf);
                        lbl->setMaximumWidth(QWIDGETSIZE_MAX);
                        lbl->setMinimumWidth(QFontMetrics(lf).horizontalAdvance(lbl == phoneWfLevelLabel ? QStringLiteral("160") : lbl->text()) + 4);
                        lbl->setAlignment(Qt::AlignHCenter);
                    }
                    else if(it->layout())
                        pending << it->layout();
                }
            }
            if(phoneWfLevelLabel)
                phoneWfLevelLabel->setMaximumHeight(QFontMetrics(lf).lineSpacing() * 2 + 2);
            QList<QWidget*> right { ui->tuneEnableChk, ui->tuneLockChk, ui->preampAttGroup };
            right << ui->preampAttGroup->findChildren<QWidget*>();
            for(QWidget *w : right)
                w->setFont(lf);
        }
        // A swipe-kill can drop settings writes that were only
        // sync()'d mid-session. Flush settings to disk whenever the app leaves the
        // foreground, the OS-sanctioned moment before suspension/termination.
        connect(qApp, &QApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state){
            if(state != Qt::ApplicationActive && settings){
                settings->beginGroup("Interface");
                settings->setValue("MainWfFloor", prefs.mainWfFloor);
                settings->setValue("SubWfFloor", prefs.subWfFloor);
                settings->endGroup();
                settings->sync();
            }
        });
        // Bottom status bar (rx latency etc.) at 14pt.
        {
            QFont sbf = statusBar()->font();
            sbf.setPointSizeF(14.0);
            statusBar()->setFont(sbf);
        }

        QWidget *newCentral = new QWidget();
        QVBoxLayout *cv = new QVBoxLayout(newCentral);
        cv->setContentsMargins(0, 0, 0, 0);
        cv->setSpacing(0);
        cv->addWidget(phoneTabs, 1);
        cv->addWidget(ui->lowerButtonsFrame, 0);

        // Reparent done above (addTab / addWidget); safe to swap the central
        // widget now (the old one, emptied, is deleted).
        setCentralWidget(newCentral);
    }

// A deliberately minimal, phone-sized connection settings page, shown as the
// 接続1 tab of the main tab widget. The phone is network-only, so only the LAN
// connection fields are needed to get on the air. A row of named connection
// profiles (rig at home / wfserver remote, etc.) sits on top; the storage
// logic is shared with wfview-mac (saveConnectionProfile & friends).
QWidget *wfmain::createPhoneConnect1Tab()
{
    // The caller puts this page beside the delay page in one 接続 tab.
    QWidget *page = new QWidget();

    QVBoxLayout *outer = new QVBoxLayout(page);
    outer->setContentsMargins(14, 6, 14, 6);
    outer->setSpacing(4);

    // Profile row: pick a saved profile, or type a new name and press 保存.
    QLabel *profLabel = new QLabel(QStringLiteral("接続プロファイル"), page);
    phoneProfileCombo1 = new QComboBox(page);
    phoneProfileCombo1->setEditable(true);
    phoneProfileCombo1->setInsertPolicy(QComboBox::NoInsert);
    if(phoneProfileCombo1->lineEdit() != Q_NULLPTR)
    {
        phoneProfileCombo1->lineEdit()->setPlaceholderText(QStringLiteral("プロファイル名"));
        phoneProfileCombo1->lineEdit()->setInputMethodHints(Qt::ImhNoPredictiveText);
    }
    phoneProfileCombo1->setMinimumHeight(30);
    QPushButton *profSaveBtn = new QPushButton(QStringLiteral("保存"), page);
    QPushButton *profDelBtn  = new QPushButton(QStringLiteral("削除"), page);
    profSaveBtn->setMinimumHeight(30);
    profDelBtn->setMinimumHeight(30);
    QHBoxLayout *profRow = new QHBoxLayout();
    profRow->addWidget(profLabel);
    profRow->addWidget(phoneProfileCombo1, 1);
    profRow->addWidget(profSaveBtn);
    profRow->addWidget(profDelBtn);
    outer->addLayout(profRow);

    QFormLayout *form = new QFormLayout();
    form->setSpacing(3);
    phoneHostEdit1 = new QLineEdit(page);
    phonePortEdit1 = new QLineEdit(page);
    phoneSerialPortEdit1 = new QLineEdit(page);
    phoneAudioPortEdit1 = new QLineEdit(page);
    phoneCivCombo1 = new QComboBox(page);
    phoneCivCombo1->setMinimumHeight(30);
    phoneUserEdit1 = new QLineEdit(page);
    phonePassEdit1 = new QLineEdit(page);
    phonePassEdit1->setEchoMode(QLineEdit::Password);

    const Qt::InputMethodHints latin = Qt::ImhLatinOnly
            | Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText;
    phoneHostEdit1->setInputMethodHints(latin);
    phoneUserEdit1->setInputMethodHints(latin);
    phonePassEdit1->setInputMethodHints(latin);
    phonePortEdit1->setInputMethodHints(Qt::ImhDigitsOnly);
    phoneSerialPortEdit1->setInputMethodHints(Qt::ImhDigitsOnly);
    phoneAudioPortEdit1->setInputMethodHints(Qt::ImhDigitsOnly);

    form->addRow(QStringLiteral("ホスト (IP / 名前)"), phoneHostEdit1);
    form->addRow(QStringLiteral("コントロールポート"), phonePortEdit1);
    // Icom rigs report the serial/audio ports at login, but wfserver and
    // non-default setups need them settable here.
    form->addRow(QStringLiteral("シリアルポート (CAT)"), phoneSerialPortEdit1);
    form->addRow(QStringLiteral("オーディオポート"), phoneAudioPortEdit1);
    form->addRow(QStringLiteral("CI-Vアドレス (機種選択)"), phoneCivCombo1);
    form->addRow(QStringLiteral("ユーザー名"), phoneUserEdit1);
    form->addRow(QStringLiteral("パスワード"), phonePassEdit1);
    outer->addLayout(form);

    // The caller places this row on the right of the 接続 tab.
    phoneConnectButtons = new QWidget();
    QHBoxLayout *btns = new QHBoxLayout(phoneConnectButtons);
    btns->setContentsMargins(14, 0, 14, 0);
    QPushButton *connectBtn = new QPushButton(QStringLiteral("保存して接続"), phoneConnectButtons);
    QPushButton *saveBtn    = new QPushButton(QStringLiteral("保存のみ"), phoneConnectButtons);
    connectBtn->setMinimumHeight(36);
    saveBtn->setMinimumHeight(36);
    btns->addWidget(connectBtn, 2);
    btns->addWidget(saveBtn, 1);

    // Selecting a saved profile loads it into prefs/udpPrefs and refills the
    // fields. `activated` fires only on user interaction, but guard against
    // programmatic changes anyway.
    connect(phoneProfileCombo1, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if(phoneUpdatingProfileCombo1 || index < 0)
            return;
        const QString name = phoneProfileCombo1->itemText(index).trimmed();
        if(!name.isEmpty())
            loadConnectionProfile(name);
    });
    connect(profSaveBtn, &QPushButton::clicked, this, [this]() {
        const QString name = phoneProfileCombo1->currentText().trimmed();
        if(name.isEmpty())
        {
            showStatusBarText(QStringLiteral("プロファイル名を入力してください"));
            return;
        }
        // Commit the on-screen fields into udpPrefs first so the profile
        // captures what the user sees, not stale values.
        applyPhoneConnect1Fields();
        saveConnectionProfile(name);
    });
    connect(profDelBtn, &QPushButton::clicked, this, [this]() {
        const QString name = phoneProfileCombo1->currentText().trimmed();
        if(!name.isEmpty())
            deleteConnectionProfile(name);
    });
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        applyPhoneConnect1Fields();
        // Keep the selected profile in sync: every save writes the on-screen
        // values into it, so each profile durably holds its own host/ports/
        // user/password.
        const QString name = phoneProfileCombo1->currentText().trimmed();
        if(!name.isEmpty())
            saveConnectionProfile(name);
        else
            showStatusBarText(QStringLiteral("接続設定を保存しました"));
    });
    connect(connectBtn, &QPushButton::clicked, this, [this]() {
        // Do NOT reload the selected profile here; that would silently discard
        // any edits made after selecting it (see SETTINGS_UI_PORTING.md §2).
        // Instead save the on-screen values into the selected profile, then
        // connect with them.
        applyPhoneConnect1Fields();
        const QString name = phoneProfileCombo1->currentText().trimmed();
        if(!name.isEmpty())
            saveConnectionProfile(name);
        if(connStatus == connDisconnected)
            connectionHandler(true);
        // Jump back to the scope tab to watch the connection come up.
        if(phoneTabs != Q_NULLPTR)
            phoneTabs->setCurrentIndex(0);
    });

    return page;
}

// Push the 接続1-tab fields into prefs/udpPrefs and persist them.
void wfmain::applyPhoneConnect1Fields()
{
    udpPrefs.ipAddress = phoneHostEdit1->text().trimmed();
    quint16 p = (quint16) phonePortEdit1->text().toUInt();
    udpPrefs.controlLANPort = p ? p : 50001;
    p = (quint16) phoneSerialPortEdit1->text().toUInt();
    udpPrefs.serialLANPort = p ? p : 50002;
    p = (quint16) phoneAudioPortEdit1->text().toUInt();
    udpPrefs.audioLANPort = p ? p : 50003;
    udpPrefs.username = phoneUserEdit1->text();
    udpPrefs.password = phonePassEdit1->text();
    udpPrefs.connectionType = connectionLAN;
    if(udpPrefs.clientName.isEmpty())
        udpPrefs.clientName = QHostInfo::localHostName();
    if(phoneCivCombo1 != Q_NULLPTR)
        prefs.radioCIVAddr = (quint8) phoneCivCombo1->currentData().toInt();
    prefs.enableLAN = true;
    prefs.hasRunSetup = true;
    prefs.settingsChanged = true;
    saveSettings();
}

// Refill the 接続1-tab fields from the current settings (startup / profile load).
void wfmain::refreshPhoneConnect1Fields()
{
    if(phoneHostEdit1 == Q_NULLPTR)
        return;
    phoneHostEdit1->setText(udpPrefs.ipAddress);
    phonePortEdit1->setText(QString::number(udpPrefs.controlLANPort ? udpPrefs.controlLANPort : 50001));
    phoneSerialPortEdit1->setText(QString::number(udpPrefs.serialLANPort ? udpPrefs.serialLANPort : 50002));
    phoneAudioPortEdit1->setText(QString::number(udpPrefs.audioLANPort ? udpPrefs.audioLANPort : 50003));
    refreshPhoneCivCombo1();
    phoneUserEdit1->setText(udpPrefs.username);
    phonePassEdit1->setText(udpPrefs.password);
}

// Fill the CI-V model combo from rigList (自動 + every known model name) and
// select the entry matching prefs.radioCIVAddr. rigList is keyed by CI-V
// address and populated from the bundled rigs/*.rig files (see
// setManufacturer()), so this only needs to run after that.
void wfmain::refreshPhoneCivCombo1()
{
    if(phoneCivCombo1 == Q_NULLPTR)
        return;
    phoneCivCombo1->clear();
    phoneCivCombo1->addItem(QStringLiteral("自動 (Auto)"), 0);
    QStringList names;
    QHash<QString, quint16> civByName;
    for(auto it = rigList.constBegin(); it != rigList.constEnd(); ++it)
    {
        if(it.value().model.isEmpty())
            continue;
        names << it.value().model;
        civByName.insert(it.value().model, it.key());
    }
    names.sort(Qt::CaseInsensitive);
    for(const QString &name : names)
    {
        quint16 civ = civByName.value(name);
        QString hex = QString("%1").arg(civ, 2, 16, QLatin1Char('0')).toUpper();
        phoneCivCombo1->addItem(QString("%1 (0x%2)").arg(name, hex), (int) civ);
    }
    int idx = phoneCivCombo1->findData((int) prefs.radioCIVAddr);
    phoneCivCombo1->setCurrentIndex(idx >= 0 ? idx : 0);
}

void wfmain::showPhoneConnect1()
{
    // The connection settings live in the 接続1 tab of the main window now.
    if(phoneTabs != Q_NULLPTR && phoneConn1TabIndex >= 0)
        phoneTabs->setCurrentIndex(phoneConn1TabIndex);
    this->raise();
    this->activateWindow();
}

// 接続2 tab: phone-wide RX/TX audio jitter-buffer delay. Kept separate from
// 接続1 (and from the per-profile storage read in loadConnectionProfile())
// because the right buffer size depends on the phone's current network, not
// on which rig/profile is selected.
QWidget *wfmain::createPhoneConnect2Tab()
{
    QWidget *page = new QWidget();
    QVBoxLayout *outer = new QVBoxLayout(page);
    outer->setContentsMargins(14, 6, 14, 6);
    outer->setSpacing(6);

    QLabel *info = new QLabel(QStringLiteral(
        "Wi-Fi/モバイル回線の状態に応じて、受信・送信の遅延バッファを調整してください。"
        "値を大きくすると音切れしにくくなりますが、応答が遅くなります。"), page);
    info->setWordWrap(true);
    outer->addWidget(info);

    QLabel *rxTitle = new QLabel(QStringLiteral("受信遅延時間 (RX Latency)"), page);
    QHBoxLayout *rxRow = new QHBoxLayout();
    phoneRxLatencySlider = new QSlider(Qt::Horizontal, page);
    phoneRxLatencySlider->setRange(30, 2000);
    phoneRxLatencySlider->setValue(prefs.rxSetup.latency);
    phoneRxLatencyValueLabel = new QLabel(QStringLiteral("%1 ms").arg(prefs.rxSetup.latency), page);
    phoneRxLatencyValueLabel->setMinimumWidth(60);
    rxRow->addWidget(phoneRxLatencySlider, 1);
    rxRow->addWidget(phoneRxLatencyValueLabel);
    outer->addWidget(rxTitle);
    outer->addLayout(rxRow);

    QLabel *txTitle = new QLabel(QStringLiteral("送信遅延時間 (TX Latency)"), page);
    QHBoxLayout *txRow = new QHBoxLayout();
    phoneTxLatencySlider = new QSlider(Qt::Horizontal, page);
    phoneTxLatencySlider->setRange(30, 2000);
    phoneTxLatencySlider->setValue(prefs.txSetup.latency);
    phoneTxLatencyValueLabel = new QLabel(QStringLiteral("%1 ms").arg(prefs.txSetup.latency), page);
    phoneTxLatencyValueLabel->setMinimumWidth(60);
    txRow->addWidget(phoneTxLatencySlider, 1);
    txRow->addWidget(phoneTxLatencyValueLabel);
    outer->addWidget(txTitle);
    outer->addLayout(txRow);

    // No Save button on the phone; persist immediately, same pattern as
    // phoneWfLevelSlider. Must land in the "LAN" group or loadSettings()/
    // saveConnectionProfile() won't find AudioRXLatency/AudioTXLatency again.
    connect(phoneRxLatencySlider, &QSlider::valueChanged, this, [this](int val) {
        prefs.rxSetup.latency = val;
        prefs.settingsChanged = true;
        if(phoneRxLatencyValueLabel)
            phoneRxLatencyValueLabel->setText(QStringLiteral("%1 ms").arg(val));
        // Apply live if already connected.
        emit sendChangeLatency(prefs.rxSetup.latency);
        if(settings) {
            settings->beginGroup("LAN");
            settings->setValue("AudioRXLatency", val);
            settings->endGroup();
            settings->sync();
        }
    });
    connect(phoneTxLatencySlider, &QSlider::valueChanged, this, [this](int val) {
        prefs.txSetup.latency = val;
        prefs.settingsChanged = true;
        if(phoneTxLatencyValueLabel)
            phoneTxLatencyValueLabel->setText(QStringLiteral("%1 ms").arg(val));
        if(settings) {
            settings->beginGroup("LAN");
            settings->setValue("AudioTXLatency", val);
            settings->endGroup();
            settings->sync();
        }
    });

    return page;
}

// Refill the 接続2-tab sliders from prefs (startup only — profile switches no
// longer touch RX/TX latency on the phone, see loadConnectionProfile()).
void wfmain::refreshPhoneConnect2Fields()
{
    if(phoneRxLatencySlider == Q_NULLPTR)
        return;
    phoneRxLatencySlider->blockSignals(true);
    phoneRxLatencySlider->setValue(prefs.rxSetup.latency);
    phoneRxLatencySlider->blockSignals(false);
    if(phoneRxLatencyValueLabel)
        phoneRxLatencyValueLabel->setText(QStringLiteral("%1 ms").arg(prefs.rxSetup.latency));

    phoneTxLatencySlider->blockSignals(true);
    phoneTxLatencySlider->setValue(prefs.txSetup.latency);
    phoneTxLatencySlider->blockSignals(false);
    if(phoneTxLatencyValueLabel)
        phoneTxLatencyValueLabel->setText(QStringLiteral("%1 ms").arg(prefs.txSetup.latency));
}

void wfmain::phoneUpdateFreq(quint64 hz)
{
    // Operate-tab mirror: group the digits with the configured separator so it
    // matches the scope-tab frequency display (e.g. 14.074.000).
    if(phoneOpFreqLabel)
    {
        QString d = QString::number(hz);
        QString g; int c = 0;
        for(int i = d.size() - 1; i >= 0; --i) {
            g.prepend(d.at(i));
            if(++c % 3 == 0 && i > 0) g.prepend(prefs.groupSeparator);
        }
        phoneOpFreqLabel->setText(g +
            QStringLiteral("<span style='font-size:17px'> MHz</span>"));
    }
}
