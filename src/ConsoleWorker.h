#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <QCommandLineParser>
#include "ConsoleReader.h"
#include "replxx.hxx"

class ConsoleWorker : public QObject
{
    Q_OBJECT
    ConsoleReader reader;
    replxx::Replxx rx;
    std::string prompt = {};
    std::string history_file_path = {"./replxx_history.txt"};
    bool needExit= false;
    void mysleep(const quint32 secs);
public:
    explicit ConsoleWorker(QObject *parent = nullptr);

public slots:
    void textReceivedCallback(const QString message);
    void run(); //метод с пользовательскими алгоритмами, которые могут быть оформлены в этом методе внутри while(1)

signals:
    void finished(); //сигнал, по которому будем завершать нить, после завершения метода run()
};

