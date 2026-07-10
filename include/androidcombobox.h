#ifndef ANDROIDCOMBOBOX_H
#define ANDROIDCOMBOBOX_H

// Qt's native QComboBox popup (a Qt::Popup-flagged top-level window) never
// creates a visible surface under this Qt-for-Android build: tapping a combo
// box, or its drop-down arrow, produces no dropdown at all (confirmed via
// on-device testing and matching a known upstream Qt/Android limitation,
// see https://forum.qt.io/topic/164645/qt6.8.3-android-qcombobox-issue).
// Combo boxes embedded through QGraphicsProxyWidget (the settings and other
// wrapped popups) are unaffected -- the proxy embeds their popups into the
// scene -- so only combos living directly on the native main window (the
// preamp / attenuator / antenna selectors) need this.
//
// installAndroidComboBoxFix() replaces the native popup with a small
// embedded QListWidget shown as a child of the combo box's own top-level
// window, so it renders on the same surface as everything else. Call it
// once per QComboBox.
class QComboBox;

void installAndroidComboBoxFix(QComboBox *combo);

#endif // ANDROIDCOMBOBOX_H
