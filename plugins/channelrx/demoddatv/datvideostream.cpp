///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2018-2019, 2021 Edouard Griffiths, F4EXB <f4exb06@gmail.com>    //
// Copyright (C) 2022 Jiří Pinkava <jiri.pinkava@rossum.ai>                      //
// Copyright (C) 2018 F4HKW                                                      //
// for F4EXB / SDRAngel                                                          //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////


#include "datvideostream.h"

#include <limits>

#include <stdio.h>

DATVideostream::DATVideostream()
{
    cleanUp();
    m_totalReceived = 0;
    m_packetReceived = 0;
    m_memoryLimit = m_defaultMemoryLimit;
    m_multiThreaded = false;
    m_threadTimeout = -1;

    m_eventLoop.connect(this, SIGNAL(dataAvailable()), &m_eventLoop, SLOT(quit()), Qt::QueuedConnection);
}

DATVideostream::~DATVideostream()
{
    m_eventLoop.disconnect(this, SIGNAL(dataAvailable()), &m_eventLoop, SLOT(quit()));
    cleanUp();
}

void DATVideostream::setThreadTimeout(int timeOut)
{
    QMutexLocker mutexLocker(&m_mutex);
    m_threadTimeout = timeOut;
    m_dataWaitCondition.wakeAll();
}

void DATVideostream::cleanUp()
{
    QMutexLocker mutexLocker(&m_mutex);

    if (m_fifo.size() > 0) {
        m_fifo.clear();
    }

    if (m_eventLoop.isRunning()) {
        m_eventLoop.exit();
    }

    m_bytesWaiting = 0;
    m_percentBuffer = 0;
    m_dataWaitCondition.wakeAll();
}

void DATVideostream::resetTotalReceived()
{
    m_totalReceived = 0;
    emit fifoData(m_bytesWaiting, m_percentBuffer, m_totalReceived);
}

void DATVideostream::setMultiThreaded(bool multiThreaded)
{
    if (multiThreaded)
    {
        if (m_eventLoop.isRunning()) {
            m_eventLoop.exit();
        }
    }

    m_multiThreaded = multiThreaded;
}

int DATVideostream::pushData(const char * chrData, int intSize)
{
    if (intSize <= 0) {
        return 0;
    }

    m_mutex.lock();

    m_packetReceived++;
    m_fifo.enqueue(QByteArray(chrData, intSize));
    m_bytesWaiting += intSize;

    while ((m_bytesWaiting > m_memoryLimit) && !m_fifo.isEmpty()) {
        m_bytesWaiting -= m_fifo.dequeue().size();
    }

    m_totalReceived += intSize;
    m_percentBuffer = (100*m_bytesWaiting) / m_memoryLimit;
    m_percentBuffer = m_percentBuffer > 100 ? 100 : m_percentBuffer;

    m_mutex.unlock();
    m_dataWaitCondition.wakeAll();

    if (m_eventLoop.isRunning()) {
        emit dataAvailable();
    }

    if (m_packetReceived % 10 == 1) {
        emit fifoData(m_bytesWaiting, m_percentBuffer, m_totalReceived);
    }

    return intSize;
}

bool DATVideostream::isSequential() const
{
    return true;
}

qint64  DATVideostream::bytesAvailable() const
{
    QMutexLocker mutexLocker(&m_mutex);
    return QIODevice::bytesAvailable() + m_bytesWaiting;
}

void  DATVideostream::close()
{
    QIODevice::close();
    cleanUp();
}

bool  DATVideostream::open(OpenMode mode)
{
    //cleanUp();
    return QIODevice::open(mode);
}

//PROTECTED

qint64 DATVideostream::readData(char *data, qint64 len)
{
    int effectiveLen = 0;
    const int expectedLen = static_cast<int>(std::min<qint64>(len, std::numeric_limits<int>::max()));

    if (expectedLen <= 0) {
        return 0;
    }

    if (m_eventLoop.isRunning()) {
        return 0;
    }

    QMutexLocker mutexLocker(&m_mutex);

    //DATA in FIFO ? -> Waiting for DATA
    while (m_fifo.isEmpty())
    {
        if (m_multiThreaded)
        {
            if (m_threadTimeout == 0) {
                return -1;
            }

            if (m_threadTimeout > 0)
            {
                if (!m_dataWaitCondition.wait(&m_mutex, m_threadTimeout) && m_fifo.isEmpty()) {
                    return -1;
                }
            }
            else
            {
                m_dataWaitCondition.wait(&m_mutex);
            }
        }
        else
        {
            mutexLocker.unlock();
            m_eventLoop.exec();
            mutexLocker.relock();

            if (m_fifo.isEmpty()) {
                return 0;
            }
        }
    }

    //Read DATA
    while ((effectiveLen < expectedLen) && !m_fifo.isEmpty())
    {
        QByteArray& currentArray = m_fifo.head();
        const int chunkLen = std::min(expectedLen - effectiveLen, static_cast<int>(currentArray.size()));
        std::copy(
            currentArray.constData(),
            currentArray.constData() + chunkLen,
            data + effectiveLen
        );

        if (chunkLen == currentArray.size()) {
            //Complete Read
            m_fifo.dequeue();
        } else {
            //Partial Read
            currentArray.remove(0, chunkLen);
        }

        effectiveLen += chunkLen;
        m_bytesWaiting -= chunkLen;
    }

    //Next available DATA
    m_percentBuffer = (100*m_bytesWaiting) / m_memoryLimit;
    m_percentBuffer = m_percentBuffer > 100 ? 100 : m_percentBuffer;

    if (m_packetReceived % 10 == 0) {
        emit fifoData(m_bytesWaiting, m_percentBuffer, m_totalReceived);
    }

    return (qint64) effectiveLen;
}

qint64 DATVideostream::writeData(const char *data, qint64 len)
{
    (void) data;
    (void) len;
    return 0;
}

qint64 	DATVideostream::readLineData(char *data, qint64 maxSize)
{
    (void) data;
    (void) maxSize;
    return 0;
}
