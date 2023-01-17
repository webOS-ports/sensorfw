/**
   @file temperatureadaptor.cpp
   @brief Contains TemperatureAdaptor

   <p>
   Copyright (C) 2016 Canonical,  Ltd.

   @author Lorn Potter <lorn.potter@canonical.com>

   This file is part of Sensorfw.

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

#include "temperatureadaptor.h"
#include "config.h"
#include "logging.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#include <QMap>

#include "datatypes/utils.h"

TemperatureAdaptor::TemperatureAdaptor(const QString& id) :
    InputDevAdaptor(id, 1)
{
    temperatureBuffer_ = new DeviceAdaptorRingBuffer<TimedUnsigned>(1);
    setAdaptedSensor("temperature", "Temperature values", temperatureBuffer_);
    setDescription("Input device temperature adaptor");
    powerStatePath_ = SensorFrameworkConfig::configuration()->value("temperature/powerstate_path").toByteArray();
    introduceAvailableDataRange(DataRange(0, 4095, 1));
    setDefaultInterval(10);
}

TemperatureAdaptor::~TemperatureAdaptor()
{
}

void TemperatureAdaptor::interpretEvent(int src, struct input_event *ev)
{
    Q_UNUSED(src);

    switch (ev->type) {
    case EV_ABS:
        switch (ev->code) {
        case ABS_X:
        case ABS_MISC:
            temperatureValue_ = ev->value;
            break;
        }
        break;
    }
}

void TemperatureAdaptor::interpretSync(int src, struct input_event *ev)
{
    Q_UNUSED(src);
    commitOutput(ev);
}

void TemperatureAdaptor::commitOutput(struct input_event *ev)
{
    TimedUnsigned* temp = temperatureBuffer_->nextSlot();
    temp->value_ = temperatureValue_;

    temp->timestamp_ = Utils::getTimeStamp(ev);

    temperatureBuffer_->commit();
    temperatureBuffer_->wakeUpReaders();
}

unsigned int TemperatureAdaptor::evaluateIntervalRequests(int& sessionId) const
{
    unsigned int highestValue = 0;
    int winningSessionId = -1;

    if (m_intervalMap.size() == 0) {
        sessionId = winningSessionId;
        return defaultInterval();
    }

    // Get the smallest positive request, 0 is reserved for HW wakeup
    QMap<int, unsigned int>::const_iterator it;
    it = m_intervalMap.begin();
    highestValue = it.value();
    winningSessionId = it.key();

    for (++it; it != m_intervalMap.constEnd(); ++it) {
        if ((it.value() < highestValue) && (it.value() > 0)) {
            highestValue = it.value();
            winningSessionId = it.key();
        }
    }

    sessionId = winningSessionId;
    return highestValue > 0 ? highestValue : defaultInterval();
}

bool TemperatureAdaptor::startSensor()
{
    if (!powerStatePath_.isEmpty()) {
        writeToFile(powerStatePath_, "1");
    }
    if (SysfsAdaptor::startSensor()) {
        return true;
    }
    return false;
}

void TemperatureAdaptor::stopSensor()
{
    if (!powerStatePath_.isEmpty()) {
        writeToFile(powerStatePath_, "0");
    }

    SysfsAdaptor::stopSensor();
}

bool TemperatureAdaptor::standby()
{
    stopSensor();
    return true;
}

bool TemperatureAdaptor::resume()
{
    startSensor();
    return true;
}
