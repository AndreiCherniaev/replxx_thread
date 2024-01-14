#include <QCoreApplication>
#include <signal.h>
#include <QCommandLineParser>
#include <QTimer>
#include "main.h"
#include "MainClass.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setApplicationName(QUOTE(MYPROJECT_NAME));
    MainClass mainClass(QCoreApplication::instance());

#ifdef Q_OS_UNIX
    struct sigaction hup;
    hup.sa_handler = mainClass.callSignalHandler;
    sigemptyset(&hup.sa_mask);
    hup.sa_flags = 0;
    hup.sa_flags |= SA_RESTART;
    if(sigaction(SIGINT, &hup, 0))
        return 1;
#else
    #warning you should use any Linux disto to get more functionality
#endif

    int ret= QCoreApplication::exec();
    return ret;

    return a.exec();
}


void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg= msg.toLocal8Bit();
    const char *file= context.file ? context.file : "";
    const char *function= context.function ? context.function : "";

    switch(type){
    case QtDebugMsg:
        fprintf(stderr, "DebugUsuall: %s\n", localMsg.constData());
        break;
    case QtInfoMsg:
        fprintf(stderr, "Info: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtWarningMsg:
        fprintf(stderr, "Warning: %s\n", localMsg.constData());
        break;
    case QtCriticalMsg:
        fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    }
}
