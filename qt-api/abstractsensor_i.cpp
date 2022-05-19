/**
   @file abstractsensor_i.cpp
   @brief Base class for sensor interface

   <p>
   Copyright (C) 2009-2010 Nokia Corporation

   @author Joep van Gassel <joep.van.gassel@nokia.com>
   @author Timo Rongas <ext-timo.2.rongas@nokia.com>
   @author Antti Virtanen <antti.i.virtanen@nokia.com>
   @author Lihan Guo <ext-lihan.4.guo@nokia.com>

   This file is part of Sensord.

   Sensord is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License
   version 2.1 as published by the Free Software Foundation.

   Sensord is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with Sensord.  If not, see <http://www.gnu.org/licenses/>.
   </p>
 */

#include "sensormanagerinterface.h"
#include "abstractsensor_i.h"
#ifdef SENSORFW_MCE_WATCHER
#include "mcewatcher.h"
#endif
#ifdef SENSORFW_LUNA_SERVICE_CLIENT
#include "lsclient.h"
#endif

struct AbstractSensorChannelInterface::AbstractSensorChannelInterfaceImpl : public QDBusAbstractInterface
{
    AbstractSensorChannelInterfaceImpl(QObject* parent, int sessionId, const QString& path, const char* interfaceName);

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    void dbusConnectNotify(const char* signal) { QDBusAbstractInterface::connectNotify(signal); }
#else
    void dbusConnectNotify(const QMetaMethod& signal) { QDBusAbstractInterface::connectNotify(signal); }
#endif
    SensorError errorCode_;
    QString errorString_;
    int sessionId_;
    int interval_;
    unsigned int bufferInterval_;
    unsigned int bufferSize_;
    SocketReader socketReader_;
    bool running_;
    bool standbyOverride_;
    bool downsampling_;
};

AbstractSensorChannelInterface::AbstractSensorChannelInterfaceImpl::AbstractSensorChannelInterfaceImpl(QObject* parent, int sessionId, const QString& path, const char* interfaceName) :
    QDBusAbstractInterface(SERVICE_NAME, path, interfaceName, QDBusConnection::systemBus(), 0),
    errorCode_(SNoError),
    errorString_(""),
    sessionId_(sessionId),
    interval_(0),
    bufferInterval_(0),
    bufferSize_(1),
    socketReader_(parent),
    running_(false),
    standbyOverride_(false),
    downsampling_(true)
{
}

AbstractSensorChannelInterface::AbstractSensorChannelInterface(const QString& path, const char* interfaceName, int sessionId) :
    pimpl_(new AbstractSensorChannelInterfaceImpl(this, sessionId, path, interfaceName))
{
    if (!pimpl_->socketReader_.initiateConnection(sessionId)) {
        setError(SClientSocketError, "Socket connection failed.");
    }
#ifdef SENSORFW_MCE_WATCHER
    MceWatcher *mcewatcher;
    mcewatcher = new MceWatcher(this);
    QObject::connect(mcewatcher,SIGNAL(displayStateChanged(bool)),
                     this,SLOT(displayStateChanged(bool)),Qt::UniqueConnection);
#endif
#ifdef SENSORFW_LUNA_SERVICE
    LSClient *lsclient;
    lsclient = new LSClient(this);
    QObject::connect(lsclient,SIGNAL(displayStateChanged(bool)),
                     this,SLOT(displayStateChanged(bool)),Qt::UniqueConnection);
#endif
}

AbstractSensorChannelInterface::~AbstractSensorChannelInterface()
{
    if ( pimpl_->isValid() )
        SensorManagerInterface::instance().releaseInterface(id(), pimpl_->sessionId_);
    if (!pimpl_->socketReader_.dropConnection())
        setError(SClientSocketError, "Socket disconnect failed.");
    delete pimpl_;
}

SocketReader& AbstractSensorChannelInterface::getSocketReader() const
{
    return pimpl_->socketReader_;
}

bool AbstractSensorChannelInterface::release()
{
    return true;
}

void AbstractSensorChannelInterface::setError(SensorError errorCode, const QString& errorString)
{
    pimpl_->errorCode_   = errorCode;
    pimpl_->errorString_ = errorString;
}

QDBusReply<void> AbstractSensorChannelInterface::start()
{
    return start(pimpl_->sessionId_);
}

QDBusReply<void> AbstractSensorChannelInterface::stop()
{
    return stop(pimpl_->sessionId_);
}

QDBusReply<void> AbstractSensorChannelInterface::start(int sessionId)
{
    clearError();

    if (pimpl_->running_) {
        return QDBusReply<void>();
    }
    pimpl_->running_ = true;

    connect(pimpl_->socketReader_.socket(), SIGNAL(readyRead()), this, SLOT(dataReceived()));

    QList<QVariant> argumentList;
    argumentList << QVariant::fromValue(sessionId);

    QDBusPendingReply <void> returnValue = pimpl_->asyncCallWithArgumentList(QLatin1String("start"), argumentList);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(returnValue, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(startFinished(QDBusPendingCallWatcher*)));

    setStandbyOverride(sessionId, pimpl_->standbyOverride_);
    setInterval(sessionId, pimpl_->interval_);
    setBufferInterval(sessionId, pimpl_->bufferInterval_);
    setBufferSize(sessionId, pimpl_->bufferSize_);
    setDownsampling(pimpl_->sessionId_, pimpl_->downsampling_);

    return returnValue;
}

void AbstractSensorChannelInterface::startFinished(QDBusPendingCallWatcher *watch)
{
    watch->deleteLater();
    QDBusPendingReply<void> reply = *watch;

    if(reply.isError()) {
        qDebug() << reply.error().message();
        setError(SHwSensorStartFailed, reply.error().message());
    }
 }

QDBusReply<void> AbstractSensorChannelInterface::stop(int sessionId)
{
    clearError();
    if (!pimpl_->running_) {
        return QDBusReply<void>();
    }
    pimpl_->running_ = false ;

    disconnect(pimpl_->socketReader_.socket(), SIGNAL(readyRead()), this, SLOT(dataReceived()));

    QList<QVariant> argumentList;
    argumentList << QVariant::fromValue(sessionId);

    QDBusPendingReply <void> returnValue = pimpl_->asyncCallWithArgumentList(QLatin1String("stop"), argumentList);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(returnValue, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(stopFinished(QDBusPendingCallWatcher*)));
    return returnValue;
}

void AbstractSensorChannelInterface::stopFinished(QDBusPendingCallWatcher *watch)
{
    watch->deleteLater();
    QDBusPendingReply<void> reply = *watch;

    if(reply.isError()) {
        qDebug() << reply.error().message();
        setError(SaCannotAccessSensor, reply.error().message());
    }
}

QDBusReply<void> AbstractSensorChannelInterface::setInterval(int sessionId, int value)
{
    clearError();

    QList<QVariant> argumentList;
    argumentList << QVariant::fromValue(sessionId) << QVariant::fromValue(value);
    QDBusPendingReply <void> returnValue = pimpl_->asyncCallWithArgumentList(QLatin1String("setInterval"), argumentList);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(returnValue, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(setIntervalFinished(QDBusPendingCallWatcher*)));
    return returnValue;
}

void AbstractSensorChannelInterface::setIntervalFinished(QDBusPendingCallWatcher *watch)
{
    watch->deleteLater();
    QDBusPendingReply<void> reply = *watch;

    if(reply.isError()) {
        qDebug() << reply.error().message();
        setError(SaCannotAccessSensor, reply.error().message());
    }
}

QDBusReply<void> AbstractSensorChannelInterface::setBufferInterval(int sessionId, unsigned int value)
{
    clearError();

    QList<QVariant> argumentList;
    argumentList << QVariant::fromValue(sessionId) << QVariant::fromValue(value);
    QDBusPendingReply <void> returnValue = pimpl_->asyncCallWithArgumentList(QLatin1String("setBufferInterval"), argumentList);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(returnValue, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(setBufferIntervalFinished(QDBusPendingCallWatcher*)));

    return returnValue;
}

void AbstractSensorChannelInterface::setBufferIntervalFinished(QDBusPendingCallWatcher *watch)
{
    watch->deleteLater();
    QDBusPendingReply<void> reply = *watch;

    if(reply.isError()) {
        qDebug() << reply.error().message();
        setError(SaCannotAccessSensor, reply.error().message());
    }
}

QDBusReply<void> AbstractSensorChannelInterface::setBufferSize(int sessionId, unsigned int value)
{
    clearError();

    QList<QVariant> argumentList;
    argumentList << QVariant::fromValue(sessionId) << QVariant::fromValue(value);

    QDBusPendingReply <void> returnValue = pimpl_->asyncCallWithArgumentList(QLatin1String("setBufferSize"), argumentList);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(returnValue, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(setBufferSizeFinished(QDBusPendingCallWatcher*)));
    return returnValue;
}

void AbstractSensorChannelInterface::setBufferSizeFinished(QDBusPendingCallWatcher *watch)
{
    watch->deleteLater();
    QDBusPendingReply<void> reply = *watch;

    if(reply.isError()) {
        qDebug() << reply.error().message();
        setError(SaCannotAccessSensor, reply.error().message());
    }
}

QDBusReply<bool> AbstractSensorChannelInterface::setStandbyOverride(int sessionId, bool value)
{
    clearError();

    QList<QVariant> argumentList;
    argumentList << QVariant::fromValue(sessionId) << QVariant::fromValue(value);
    QDBusPendingReply <void> returnValue = pimpl_->asyncCallWithArgumentList(QLatin1String("setStandbyOverride"), argumentList);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(returnValue, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(setStandbyOverrideFinished(QDBusPendingCallWatcher*)));
    return returnValue;
}

void AbstractSensorChannelInterface::setStandbyOverrideFinished(QDBusPendingCallWatcher *watch)
{
    watch->deleteLater();
    QDBusPendingReply<void> reply = *watch;

    if(reply.isError()) {
        qDebug() << reply.error().message();
        setError(SaCannotAccessSensor, reply.error().message());
    }
}

DataRangeList AbstractSensorChannelInterface::getAvailableDataRanges()
{
    return getAccessor<DataRangeList>("getAvailableDataRanges");
}

DataRange AbstractSensorChannelInterface::getCurrentDataRange()
{
    return getAccessor<DataRange>("getCurrentDataRange");
}

void AbstractSensorChannelInterface::requestDataRange(DataRange range)
{
    clearError();
    call(QDBus::NoBlock, QLatin1String("requestDataRange"), QVariant::fromValue(pimpl_->sessionId_), QVariant::fromValue(range));
}

void AbstractSensorChannelInterface::removeDataRangeRequest()
{
    clearError();
    call(QDBus::NoBlock, QLatin1String("removeDataRangeRequest"), QVariant::fromValue(pimpl_->sessionId_));
}

DataRangeList AbstractSensorChannelInterface::getAvailableIntervals()
{
    return getAccessor<DataRangeList>("getAvailableIntervals");
}

IntegerRangeList AbstractSensorChannelInterface::getAvailableBufferIntervals()
{
    return getAccessor<IntegerRangeList>("getAvailableBufferIntervals");
}

IntegerRangeList AbstractSensorChannelInterface::getAvailableBufferSizes()
{
    return getAccessor<IntegerRangeList>("getAvailableBufferSizes");
}

bool AbstractSensorChannelInterface::hwBuffering()
{
    return getAccessor<bool>("hwBuffering");
}

int AbstractSensorChannelInterface::sessionId() const
{
    return pimpl_->sessionId_;
}

SensorError AbstractSensorChannelInterface::errorCode()
{
    if (pimpl_->errorCode_ != SNoError) {
        return pimpl_->errorCode_;
    }
    return static_cast<SensorError>(getAccessor<int>("errorCodeInt"));
}

QString AbstractSensorChannelInterface::errorString()
{
    if (pimpl_->errorCode_ != SNoError)
        return pimpl_->errorString_;
    return getAccessor<QString>("errorString");
}

QString AbstractSensorChannelInterface::description()
{
    return getAccessor<QString>("description");
}

QString AbstractSensorChannelInterface::id()
{
    return getAccessor<QString>("id");
}

int AbstractSensorChannelInterface::interval()
{
    if (pimpl_->running_)
        return static_cast<int>(getAccessor<unsigned int>("interval"));
    return pimpl_->interval_;
}

void AbstractSensorChannelInterface::setInterval(int value)
{
    pimpl_->interval_ = value;
    if (pimpl_->running_)
        setInterval(pimpl_->sessionId_, value);
}

unsigned int AbstractSensorChannelInterface::bufferInterval()
{
    if (pimpl_->running_)
        return getAccessor<unsigned int>("bufferInterval");
    return pimpl_->bufferInterval_;
}

void AbstractSensorChannelInterface::setBufferInterval(unsigned int value)
{
    pimpl_->bufferInterval_ = value;
    if (pimpl_->running_)
        setBufferInterval(pimpl_->sessionId_, value);
}

unsigned int AbstractSensorChannelInterface::bufferSize()
{
    if (pimpl_->running_)
        return getAccessor<unsigned int>("bufferSize");
    return pimpl_->bufferSize_;
}

void AbstractSensorChannelInterface::setBufferSize(unsigned int value)
{
    pimpl_->bufferSize_ = value;
    if (pimpl_->running_)
        setBufferSize(pimpl_->sessionId_, value);
}

bool AbstractSensorChannelInterface::standbyOverride()
{
    if (pimpl_->running_)
        return getAccessor<unsigned int>("standbyOverride");
    return pimpl_->standbyOverride_;
}

bool AbstractSensorChannelInterface::setStandbyOverride(bool override)
{
    pimpl_->standbyOverride_ = override;
    return setStandbyOverride(pimpl_->sessionId_, override);
}

QString AbstractSensorChannelInterface::type()
{
    return getAccessor<QString>("type");
}

void AbstractSensorChannelInterface::clearError()
{
    pimpl_->errorCode_ = SNoError;
    pimpl_->errorString_.clear();
}

void AbstractSensorChannelInterface::dataReceived()
{
    do
    {
        if(!dataReceivedImpl())
            return;
    } while(pimpl_->socketReader_.socket()->bytesAvailable());
}

bool AbstractSensorChannelInterface::read(void* buffer, int size)
{
    return pimpl_->socketReader_.read(buffer, size);
}

bool AbstractSensorChannelInterface::setDataRangeIndex(int dataRangeIndex)
{
    clearError();
    QList<QVariant> argumentList;
    argumentList <<  QVariant::fromValue(pimpl_->sessionId_) << QVariant::fromValue(dataRangeIndex);

    QDBusPendingReply <bool> returnValue = pimpl_->asyncCallWithArgumentList(QLatin1String("setDataRangeIndex"), argumentList);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(returnValue, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(setDataRangeIndexFinished(QDBusPendingCallWatcher*)));
    return returnValue;
}

void AbstractSensorChannelInterface::setDataRangeIndexFinished(QDBusPendingCallWatcher *watch)
{
    watch->deleteLater();
    QDBusPendingReply<void> reply = *watch;

    if(reply.isError()) {
        qDebug() << reply.error().message();
        setError(SaCannotAccessSensor, reply.error().message());
    }
}

QDBusMessage AbstractSensorChannelInterface::call(QDBus::CallMode mode,
                                                  const QString& method,
                                                  const QVariant& arg1,
                                                  const QVariant& arg2,
                                                  const QVariant& arg3,
                                                  const QVariant& arg4,
                                                  const QVariant& arg5,
                                                  const QVariant& arg6,
                                                  const QVariant& arg7,
                                                  const QVariant& arg8)
{
    return pimpl_->call(mode, method, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
}

QDBusMessage AbstractSensorChannelInterface::callWithArgumentList(QDBus::CallMode mode, const QString& method, const QList<QVariant>& args)
{
    return pimpl_->call(mode, method, args);
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
void AbstractSensorChannelInterface::dbusConnectNotify(const char* signal)
#else
void AbstractSensorChannelInterface::dbusConnectNotify(const QMetaMethod& signal)
#endif
{
    pimpl_->dbusConnectNotify(signal);
}

bool AbstractSensorChannelInterface::isValid() const
{
    return pimpl_->isValid();
}

bool AbstractSensorChannelInterface::downsampling()
{
    return pimpl_->downsampling_;
}

bool AbstractSensorChannelInterface::setDownsampling(bool value)
{
    pimpl_->downsampling_ = value;
    return setDownsampling(pimpl_->sessionId_, value).isValid();
}

QDBusReply<void> AbstractSensorChannelInterface::setDownsampling(int sessionId, bool value)
{
    clearError();

    QList<QVariant> argumentList;
    argumentList <<  QVariant::fromValue(sessionId) << QVariant::fromValue(value);
    QDBusPendingReply <void> returnValue = pimpl_->asyncCallWithArgumentList(QLatin1String("setDownsampling"), argumentList);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(returnValue, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            SLOT(setDownsamplingFinished(QDBusPendingCallWatcher*)));
    return returnValue;
}

void AbstractSensorChannelInterface::setDownsamplingFinished(QDBusPendingCallWatcher *watch)
{
    watch->deleteLater();
    QDBusPendingReply<void> reply = *watch;

    if(reply.isError()) {
        qDebug() << reply.error().message();
        setError(SaCannotAccessSensor, reply.error().message());
    }
}

void AbstractSensorChannelInterface::displayStateChanged(bool displayState)
{
    if (!pimpl_->standbyOverride_) {
        if (!displayState) {
            stop();
        } else {
            start();
        }
    }
}

