#ifdef BUILD_WFSERVER
#include <QtCore/QCoreApplication>
#include "keyboard.h"
#else
#include <QApplication>
#include <QStatusBar>
#include <QTranslator>
#ifdef Q_OS_ANDROID
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsProxyWidget>
#include <QScreen>
#include <QResizeEvent>
#include <QTransform>
#include <QLayout>
#endif
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <csignal>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

#include <iostream>
#ifdef BUILD_WFSERVER
#include "servermain.h"
#include "serverwizard.h"
#else
#include "wfmain.h"
#endif
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "logcategories.h"

bool debugMode=false;

#ifdef BUILD_WFSERVER
// Smart pointer to log file
QScopedPointer<QFile>   m_logFile;
QMutex logMutex;
servermain* w=Q_NULLPTR;

#ifdef Q_OS_WIN
bool __stdcall cleanup(DWORD sig)
 #else
static void cleanup(int sig)
 #endif
{
    switch(sig) {
#ifndef Q_OS_WIN
    case SIGHUP:
        qInfo() << "hangup signal";
        break;
#endif
    case SIGTERM:
        qInfo() << "terminate signal caught";
        if (w!=Q_NULLPTR) w->deleteLater();
        QCoreApplication::quit();
        break;
    default:
        break;
    }

 #ifdef Q_OS_WIN
    return true;
 #else
    return;
 #endif
}


 #ifndef Q_OS_WIN
void initDaemon()
{
    int i;
    if(getppid()==1)
        return; /* already a daemon */
    i=fork();
    if (i<0)
        exit(1); /* fork error */
    if (i>0)
        exit(0); /* parent exits */

    setsid(); /* obtain a new process group */

    for (i=getdtablesize();i>=0;--i)
        close(i); /* close all descriptors */
    i=open("/dev/null",O_RDWR); dup(i); dup(i);

    signal(SIGCHLD,SIG_IGN);
    signal(SIGTSTP,SIG_IGN);
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);
}

 #else

void initDaemon() {
    std::cout << "Background mode does not currently work in Windows\n";
    exit(1);
}

 #endif

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);
#endif

#ifdef Q_OS_ANDROID
// QScreen::availableGeometry(), queried once up front, was observed to
// reflect the device's natural (portrait) orientation rather than its
// current (rotated to landscape) one, producing a pillarboxed portrait-
// shaped render inside the landscape screen. Recomputing the scale
// transform from the QGraphicsView's own viewport size on every resize
// (rather than from a screen-geometry query taken once at startup) always
// matches what Android actually handed the window, in whatever orientation.
class FitToScreenView : public QGraphicsView
{
public:
    FitToScreenView(QGraphicsScene *scene, QSize naturalSize)
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
    // The embedded central widget can end up larger than the reference
    // size captured at startup (the proxy enforces the layout's minimum
    // size, which grows as controls are added or rig-dependent groups
    // appear), which clipped the right edge. Re-read the live size from
    // the scene on every fit.
    void refit()
    {
        QSizeF s = naturalSize;
        if (scene() != Q_NULLPTR) {
            // Use the real bounding rect (its origin can be negative when a
            // widget pokes left of the origin); anchoring at (0,0) would
            // clip the left/top edge.
            const QRectF r = scene()->itemsBoundingRect();
            if (!r.isEmpty()) {
                s = r.size();
                setSceneRect(r);
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
#endif

int main(int argc, char *argv[])
{

#ifdef BUILD_WFSERVER
    QCoreApplication a(argc, argv);
    a.setOrganizationName("wfview");
    a.setOrganizationDomain("wfview.org");
    a.setApplicationName("wfserver");
    keyboard* kb = Q_NULLPTR;
#else
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#ifdef Q_OS_ANDROID
    // wfview's layouts are sized in fixed (desktop-era) pixel amounts.
    // Qt 6's automatic high-DPI scaling (unconditional, unlike Qt 5)
    // blows those up to match this device's DPI, so the UI ends up far
    // wider/taller than the physical screen with no way to scroll to
    // the rest. Rendering at a literal 1:1 pixel ratio instead keeps
    // the original desktop proportions, which happen to roughly match
    // this class of tablet's raw resolution.
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
#endif
    QApplication a(argc, argv);
    a.setOrganizationName("wfview");
    a.setOrganizationDomain("wfview.org");
    a.setApplicationName("wfview");
    a.setDesktopFileName("wfview");
#endif

#ifdef QT_DEBUG
    //debugMode = true;
#endif

    QDateTime date = QDateTime::currentDateTime();
    QString formattedTime = date.toString("dd.MM.yyyy hh:mm:ss");
    QString logFilename = (QString("%1/%2-%3.log").arg(QStandardPaths::standardLocations(QStandardPaths::TempLocation)[0]).arg(a.applicationName()).arg(date.toString("yyyyMMddhhmmss")));

    QString settingsFile = NULL;
    QString currentArg;
#ifdef BUILD_WFSERVER
    bool runSetup = false;
#endif


    const QString helpText = QString("\nUsage: -l --logfile filename.log, -s --settings filename.ini, -c --clearconfig CONFIRM, -b --background (not Windows), -d --debug, -v --version"
#ifdef BUILD_WFSERVER
                                     ", --setup (interactive config wizard)"
#endif
                                     "\n"); // TODO...
#ifdef BUILD_WFSERVER
    const QString version = QString("wfserver version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
        .arg(QString(WFVIEW_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());
#else
    const QString version = QString("wfview version: %1 (Git:%2 on %3 at %4 by %5@%6)\nOperating System: %7 (%8)\nBuild Qt Version %9. Current Qt Version: %10\n")
        .arg(QString(WFVIEW_VERSION))
        .arg(GITSHORT).arg(__DATE__).arg(__TIME__).arg(UNAME).arg(HOST)
        .arg(QSysInfo::prettyProductName()).arg(QSysInfo::buildCpuArchitecture())
        .arg(QT_VERSION_STR).arg(qVersion());

    // Translator doesn't really make sense for wfserver right now.
    QTranslator myappTranslator;
    qDebug() << "Current translation language: " << myappTranslator.language();

    bool trResult = myappTranslator.load(QLocale(), QLatin1String("wfview"), QLatin1String("_"), QLatin1String(":/translations"));
    if(trResult) {
        qDebug() << "Recognized requested language and loaded the translations (or at least found the /translations resource folder). Installing translator.";
        a.installTranslator(&myappTranslator);
    } else {
        qDebug() << "Could not load translation.";
    }

    qDebug() << "Changed to translation language: " << myappTranslator.language();
#endif

    for(int c=1; c<argc; c++)
    {
        //qInfo() << "Argc: " << c << " argument: " << argv[c];
        currentArg = QString(argv[c]);

        if ((currentArg == "-d") || (currentArg == "--debug"))
        {
            debugMode = true;
        }
        else if ((currentArg == "-l") || (currentArg == "--logfile"))
        {
            if (argc > c)
            {
                logFilename = argv[c + 1];
                c += 1;
            }
        }
        else if ((currentArg == "-s") || (currentArg == "--settings"))
        {
            if (argc > c)
            {
                settingsFile = argv[c + 1];
                c += 1;
            }
        }
        else if ((currentArg == "-c") || (currentArg == "--clearconfig"))
        {
            if (argc > c)
            {
                QString confirm = argv[c + 1];
                c += 1;
                if (confirm == "CONFIRM") {
                    QSettings* settings;
                    // Clear config
                    if (settingsFile.isEmpty()) {
                        settings = new QSettings();
                    }
                    else
                    {
                        QString file = settingsFile;
                        QFile info(settingsFile);
                        QString path="";
                        if (!QFileInfo(info).isAbsolute())
                        {
                            path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                            if (path.isEmpty())
                            {
                                path = QDir::homePath();
                            }
                            path = path + "/";
                            file = info.fileName();
                        }
                        settings = new QSettings(path + file, QSettings::Format::IniFormat);
                    }
                    settings->clear();

                    // Derive the companion .noise profile path the same way wfmain does.
                    QString noisePath;
                    const QString sf = settings->fileName();
                    const QFileInfo fi(sf);
                    if (!sf.isEmpty() && fi.suffix().length() >= 2 && fi.suffix().length() <= 5
                            && fi.absoluteDir().exists()) {
                        noisePath = fi.absoluteDir().absoluteFilePath(fi.baseName() + ".noise");
                    } else {
                        QString appData = QStandardPaths::writableLocation(
                                              QStandardPaths::AppDataLocation);
                        if (appData.isEmpty())
                            appData = QDir::homePath();
                        noisePath = appData + "/wfview.noise";
                    }

                    // Overwrite the noise file with an empty profile store so
                    // stale profiles from the old configuration are discarded.
                    QJsonObject emptyRoot;
                    emptyRoot["version"]  = 1;
                    emptyRoot["profiles"] = QJsonObject();
                    QSaveFile nf(noisePath);
                    if (nf.open(QIODevice::WriteOnly)) {
                        nf.write(QJsonDocument(emptyRoot).toJson(QJsonDocument::Compact));
                        if (nf.commit())
                            std::cout << QString("ANR noise profiles cleared: %1\n")
                                             .arg(noisePath).toStdString();
                        else
                            std::cout << QString("Warning: could not write noise profile file: %1\n")
                                             .arg(noisePath).toStdString();
                    } else {
                        std::cout << QString("Warning: could not open noise profile file for writing: %1\n")
                                         .arg(noisePath).toStdString();
                    }

                    delete settings;
                    std::cout << QString("All wfview settings cleared.\n").toStdString();
                    exit(0);
                }
            }
            std::cout << QString("Error: Clear config not confirmed (please add the word CONFIRM), aborting\n").toStdString();
            std::cout << helpText.toStdString();
            exit(-1);
        }
#ifdef BUILD_WFSERVER
        else if ((currentArg == "-b") || (currentArg == "--background"))
        {
            initDaemon();
        }
        else if (currentArg == "--setup")
        {
            runSetup = true;
        }
#endif
        else if ((currentArg == "-?") || (currentArg == "--help"))
        {
            std::cout << helpText.toStdString();
            return 0;
        }
        else if ((currentArg == "-v") || (currentArg == "--version"))
        {
            std::cout << version.toStdString();
            return 0;
	}
        else {
            std::cout << "Unrecognized option: " << currentArg.toStdString();
            std::cout << helpText.toStdString();
            return -1;
        }

    }

#ifdef BUILD_WFSERVER

    // Set the logging file before doing anything else.
    m_logFile.reset(new QFile(logFilename));
    // Open the file logging
    m_logFile.data()->open(QFile::WriteOnly | QFile::Truncate | QFile::Text);
    // Set handler
    qInstallMessageHandler(messageHandler);

    qInfo(logSystem()) << version;

#endif

#ifdef BUILD_WFSERVER
    if (runSetup) {
        // Run the wizard before installing signal handlers so Ctrl-C aborts.
        int rc = serverwizard::run(settingsFile);
        return rc;
    }
 #ifdef Q_OS_WIN
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)cleanup, TRUE);
 #else
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGKILL, cleanup);
 #endif
    kb = new keyboard();
    kb->start();
    w = new servermain(settingsFile);
#else
    a.setWheelScrollLines(1); // one line per wheel click
    wfmain w(settingsFile, logFilename, debugMode);
#ifdef Q_OS_ANDROID
    // wfview's layout is sized in fixed desktop-era pixel amounts and does
    // not stretch to fill an arbitrary phone/tablet screen: shown directly,
    // it either overflows off the physical screen edges (wider axis) or
    // leaves blank space (shorter axis), with no way to reach the
    // off-screen part since nothing scrolls. Rather than rework every
    // layout to be screen-size-aware, host wfmain's central widget inside
    // a QGraphicsView via QGraphicsProxyWidget and apply a transform that
    // scales its natural (unscaled) size to exactly match the screen's
    // available geometry -- this fills the screen exactly with no
    // scrollbars and no cut-off edges.
    //
    // wfmain itself (the QMainWindow) is deliberately never shown: embedding
    // it directly (as a top-level widget) left Android's platform plugin
    // still compositing wfmain's own native surface on top of the scaled
    // proxy, producing a doubled/ghosted display. wfmain has no menu bar,
    // toolbar, status bar or dock widgets (confirmed against wfmain.ui), so
    // its central widget alone is the entire UI; detaching it via
    // takeCentralWidget() and embedding *that* (an ordinary child widget,
    // never itself top-level) avoids the duplicate-surface problem.
    // Pre-size the window to a generous reference size before detaching its
    // central widget, so Expanding-policy children (like the waterfall/
    // scope plots) claim their intended share of space the same way they
    // would if a desktop user resized/maximized the window. Measuring
    // sizeHint()/adjustSize() on an unshown window instead reports every
    // panel at its bare minimum, which left the waterfall reduced to a
    // sliver once that cramped layout was scaled up to fill the screen.
    w.ensurePolished();
    // Match the 5:3 aspect of the target screen (2000x1200): with the status
    // bar (~48) and margins (~14) the container totals ~2400x1440, so the
    // fit-to-screen transform scales both axes equally and circles (the
    // tuning dial) stay circular.
    w.resize(2400, 1378);
    if (w.layout())
        w.layout()->activate();
    const QSize naturalSize = w.size();
    QWidget *central = w.takeCentralWidget();

    // The status bar (rx latency, connection messages) belongs to the
    // QMainWindow, not the central widget, so it would never be shown on
    // Android. Stack the two in a plain container and embed that instead.
    QWidget *container = new QWidget();
    QVBoxLayout *containerLayout = new QVBoxLayout(container);
    // A small margin keeps edge-hugging widgets (meter scale, bottom row)
    // from being clipped by the exact-fit scaling.
    containerLayout->setContentsMargins(10, 2, 10, 12);
    containerLayout->setSpacing(0);
    containerLayout->addWidget(central, 1);
    QStatusBar *mainStatusBar = w.statusBar();
    mainStatusBar->setParent(container);
    containerLayout->addWidget(mainStatusBar, 0);
    // Re-apply the generous reference size to the container: embedding sizes
    // the proxy from the layout minimum otherwise, collapsing the Expanding
    // scope panel to a sliver (see the pre-size comment above).
    container->setMinimumSize(naturalSize.width(),
                              naturalSize.height() + mainStatusBar->sizeHint().height());
    container->resize(container->minimumSize());
    containerLayout->activate();

    QGraphicsScene *scene = new QGraphicsScene();
    QGraphicsProxyWidget *proxy = scene->addWidget(container);

    FitToScreenView *view = new FitToScreenView(scene, naturalSize);
    // Track later growth of the embedded widget (rig-dependent groups
    // appearing after connect, etc.) so nothing gets scaled off-screen.
    QObject::connect(proxy, &QGraphicsWidget::geometryChanged, view,
                     [view]() { view->refit(); });
    view->setFrameShape(QFrame::NoFrame);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setSceneRect(0, 0, naturalSize.width(), naturalSize.height());

    view->showFullScreen();
#else
    w.show();
#endif

#endif
    return a.exec();

}

#ifdef BUILD_WFSERVER

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Open stream file writes
    if (type == QtDebugMsg && !debugMode)
    {
        return;
    }
    QMutexLocker locker(&logMutex);
    QTextStream out(m_logFile.data());
    QString text;

    // Write the date of recording
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");
    // By type determine to what level belongs message
    
    switch (type)
    {
        case QtDebugMsg:
            out << "DBG ";
            break;
        case QtInfoMsg:
            out << "INF ";
            break;
        case QtWarningMsg:
            out << "WRN ";
            break;
        case QtCriticalMsg:
            out << "CRT ";
            break;
        case QtFatalMsg:
            out << "FTL ";
            break;
    } 
    // Write to the output category of the message and the message itself
    out << context.category << ": " << msg << "\n";
    std::cout << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ").toLocal8Bit().toStdString() << msg.toLocal8Bit().toStdString() << "\n";
    out.flush();    // Clear the buffered data
}
#endif
