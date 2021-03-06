﻿#include "pm3process.h"

PM3Process::PM3Process(QThread* thread, QObject* parent): QProcess(parent)
{
    moveToThread(thread);
    setProcessChannelMode(PM3Process::MergedChannels);
    isRequiringOutput = false;
    requiredOutput = new QString();
    serialListener = new QTimer(); // if using new QTimer(this), the debug output will show "Cannot create children for a parent that is in a different thread."
    serialListener->moveToThread(this->thread());// I've tried many ways to creat a QTimer instance, but all of the instances are in the main thread(UI thread), so I have to move it manually
    serialListener->setInterval(1000);
    serialListener->setTimerType(Qt::VeryCoarseTimer);
    connect(serialListener, &QTimer::timeout, this, &PM3Process::onTimeout);
    connect(this, &PM3Process::readyRead, this, &PM3Process::onReadyRead);
}

void PM3Process::connectPM3(const QString& path, const QString& port, const QStringList args)
{
    QString result;
    Util::ClientType clientType;
    setRequiringOutput(true);

    // stash for reconnect
    currPath = path;
    currPort = port;
    currArgs = args;

    // using "-f" option to make the client output flushed after every print.
    start(path, args, QProcess::Unbuffered | QProcess::ReadWrite);
    if(waitForStarted(10000))
    {
        waitForReadyRead(10000);
        setRequiringOutput(false);
        result = *requiredOutput;
        if(result.indexOf("[=]") != -1)
        {
            clientType = Util::CLIENTTYPE_ICEMAN;
            setRequiringOutput(true);
            write("hw version\r\n");
            for(int i = 0; i < 10; i++)
            {
                waitForReadyRead(200);
                result += *requiredOutput;
            }
            setRequiringOutput(false);
        }
        else
        {
            clientType = Util::CLIENTTYPE_OFFICIAL;
        }
        if(result.indexOf("os: ") != -1) // make sure the PM3 is connected
        {
            emit changeClientType(clientType);
            result = result.mid(result.indexOf("os: "));
            result = result.left(result.indexOf("\r\n"));
            result = result.mid(3, result.lastIndexOf(" ") - 3);
            emit PM3StatedChanged(true, result);

            // if the arguments don't contain <port>, then disable the port listener
            // useful when using offline sniff
            if(args.indexOf(port) != -1)
                setSerialListener(port, true);
        }
        else
            kill();
    }
}

void PM3Process::reconnectPM3()
{
    connectPM3(currPath, currPort, currArgs);
}

void PM3Process::setRequiringOutput(bool st)
{
    isRequiringOutput = st;
    if(isRequiringOutput)
        requiredOutput->clear();
}

bool PM3Process::waitForReadyRead(int msecs)
{
    return QProcess::waitForReadyRead(msecs);
}

void PM3Process::setSerialListener(const QString& name, bool state)
{
    if(state)
    {
        portInfo = new QSerialPortInfo(name);
        serialListener->start();
        qDebug() << serialListener->thread();
    }
    else
    {
        serialListener->stop();
        delete portInfo;
    }
}

void PM3Process::onTimeout() //when the proxmark3 client is unexpectedly terminated or the PM3 hardware is removed, the isBusy() will return false(only tested on Windows);
{
//    qDebug()<<portInfo->isBusy();
    if(!portInfo->isBusy())
    {
        kill();
        emit PM3StatedChanged(false);
        setSerialListener("", false);
    }
}

void PM3Process::testThread()
{
    qDebug() << "PM3:" << QThread::currentThread();
}

qint64 PM3Process::write(QString data)
{
    return QProcess::write(data.toLatin1());
}

void PM3Process::onReadyRead()
{
    QString out = readAll();
    if(isRequiringOutput)
        requiredOutput->append(out);
    if(out != "")
    {
//        qDebug() << "PM3Process::onReadyRead:" << out;
        emit newOutput(out);

    }
}

void PM3Process::setProcEnv(const QStringList* env)
{
//    qDebug() << "passed Env List" << *env;
    this->setEnvironment(*env);
    //    qDebug() << "final Env List" << processEnvironment().toStringList();
}

void PM3Process::setWorkingDir(const QString& dir)
{
    // the working directory cannot be the default, or the client will failed to load the dll
    this->setWorkingDirectory(dir);
}
