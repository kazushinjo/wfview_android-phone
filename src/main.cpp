#ifdef BUILD_WFSERVER
#include <QtCore/QCoreApplication>
#include "keyboard.h"
#else
#include <QApplication>
#include <QStatusBar>
#include <QTranslator>
#ifdef Q_OS_ANDROID
#include <QScreen>
#include <QLayout>
#include "androidcompat.h"
#include <QJniObject>
#include <QCoreApplication>
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
#ifdef WFVIEW_PHONE
    // Phone build: the layout follows the iPhone port, whose sizes are in
    // iPhone points (landscape short side 393). Keep Qt's high-DPI scaling and
    // pick the factor that maps this phone's short side to 393 logical
    // pixels, and render 1pt as 1 logical pixel as iOS does, so the same
    // numbers give the same proportions on any phone.
    {
        QJniObject res = QJniObject::callStaticObjectMethod(
            "android/content/res/Resources", "getSystem", "()Landroid/content/res/Resources;");
        QJniObject dm = res.isValid()
            ? res.callObjectMethod("getDisplayMetrics", "()Landroid/util/DisplayMetrics;")
            : QJniObject();
        if (dm.isValid()) {
            // Qt's own device pixel ratio on Android is 1, so the scale
            // factor alone sets physical pixels per logical pixel.
            const int wpx = dm.getField<jint>("widthPixels");
            const int hpx = dm.getField<jint>("heightPixels");
            if (wpx > 0 && hpx > 0) {
                const double factor = qMin(wpx, hpx) / 393.0;
                qputenv("QT_SCALE_FACTOR", QByteArray::number(factor, 'f', 4));
            }
        }
        qputenv("QT_FONT_DPI", "72");
    }
#else
    // wfview's layouts are sized in fixed (desktop-era) pixel amounts.
    // Qt 6's automatic high-DPI scaling (unconditional, unlike Qt 5)
    // blows those up to match this device's DPI, so the UI ends up far
    // wider/taller than the physical screen with no way to scroll to
    // the rest. Rendering at a literal 1:1 pixel ratio instead keeps
    // the original desktop proportions, which happen to roughly match
    // this class of tablet's raw resolution.
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
#endif
#endif
    QApplication a(argc, argv);
    a.setOrganizationName("wfview4android");
    a.setOrganizationDomain("wfview.org");
    a.setApplicationName("wfview4android");
    a.setDesktopFileName("wfview4android");
#ifdef Q_OS_ANDROID
#ifndef WFVIEW_PHONE
    // With high-DPI scaling disabled, widget fonts would render at the
    // same pixel size on every device; scale the application font by the
    // same design-to-device factor as the widget dimensions so text keeps
    // its proportion of the screen on any resolution.
    {
        QFont f = a.font();
        if (f.pointSizeF() > 0)
            f.setPointSizeF(f.pointSizeF() * androidUiScale());
        else if (f.pixelSize() > 0)
            f.setPixelSize(qMax(1, androidDp(f.pixelSize())));
        a.setFont(f);
    }
#endif
#endif
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
    // Responsive main window: shown natively fullscreen, its layouts
    // stretch to whatever screen Android provides. All Android-specific
    // fixed pixel amounts are design pixels mapped through androidDp()
    // (see androidcompat.h), so proportions match the reference layout on
    // any resolution/aspect while Expanding panels (scope/waterfall)
    // absorb the remaining space.
    w.ensurePolished();
    // A small margin keeps edge-hugging widgets (meter scale, bottom row)
    // from touching the physical screen edges.
    if (w.centralWidget() != Q_NULLPTR && w.centralWidget()->layout() != Q_NULLPTR) {
        w.centralWidget()->layout()->setContentsMargins(
            androidDp(12), androidDp(2), androidDp(12), androidDp(2));
        // Move the status bar into the central layout: as the QMainWindow's
        // own status bar its internal reformat() ignores the widget's
        // contentsMargins, so the tuned left indent of the status readouts
        // never applies. As a plain layout row (same as the pre-responsive
        // container) the margins work.
        QStatusBar *sb = w.statusBar();
        sb->setParent(w.centralWidget());
        w.centralWidget()->layout()->addWidget(sb);
#ifdef WFVIEW_PHONE
        // The status text uses a larger font on the phone; give the row the
        // height it needs and let the tab area absorb the difference, or the
        // bottom of the text is clipped.
        sb->setFixedHeight(qMax(sb->sizeHint().height(), sb->minimumSizeHint().height()));
#endif
    }
#ifdef WFVIEW_PHONE
    // Phone: lay out inside the area left by the display cutout, and hide the
    // status and navigation bars (a swipe from the edge shows them briefly) so
    // the landscape layout gets the full height, as on the iPhone.
    w.showMaximized();
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([]() {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (!window.isValid())
            return;
        const jint sdk = QJniObject::getStaticField<jint>("android/os/Build$VERSION", "SDK_INT");
        if (sdk >= 30) {
            QJniObject ctl = window.callObjectMethod("getInsetsController",
                                                     "()Landroid/view/WindowInsetsController;");
            if (ctl.isValid()) {
                const jint bars = QJniObject::callStaticMethod<jint>(
                    "android/view/WindowInsets$Type", "systemBars", "()I");
                ctl.callMethod<void>("hide", "(I)V", bars);
                // BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                ctl.callMethod<void>("setSystemBarsBehavior", "(I)V", 2);
            }
        } else {
            QJniObject decor = window.callObjectMethod("getDecorView", "()Landroid/view/View;");
            // IMMERSIVE_STICKY | FULLSCREEN | HIDE_NAVIGATION | LAYOUT_STABLE
            if (decor.isValid())
                decor.callMethod<void>("setSystemUiVisibility", "(I)V", 0x1000 | 0x4 | 0x2 | 0x100);
        }
    });
#else
    w.showFullScreen();
#endif
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
