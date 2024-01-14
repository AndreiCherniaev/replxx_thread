//https://stackoverflow.com/questions/6878507/using-qsocketnotifier-to-select-on-a-char-device/7389622#7389622
#include "ConsoleReader.h"
#include <QTextStream>
#include <iostream>
#include <unistd.h> //Provides STDIN_FILENO

ConsoleReader::ConsoleReader(QObject *parent) :
    QObject(parent),
    notifier(new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this))
{
    connect(notifier, &QSocketNotifier::activated, this, &ConsoleReader::text);
}

void ConsoleReader::text(QSocketDescriptor d, QSocketNotifier::Type t)
{
    emit textDetected();
}
