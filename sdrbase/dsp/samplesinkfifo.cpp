///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2016, 2018-2019, 2021-2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com> //
// Copyright (C) 2022 Jiří Pinkava <jiri.pinkava@rossum.ai>                      //
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

#include "samplesinkfifo.h"

#include "maincore.h"

//#define MIN(x, y) (((x) < (y)) ? (x) : (y))

void SampleSinkFifo::reset()
{
    m_suppressed = -1;
    m_core.reset();
}

SampleSinkFifo::SampleSinkFifo() :
    m_total(0),
    m_writtenSignalCount(0),
    m_writtenSignalRateDivider(1)
{
    m_suppressed = -1;
}

SampleSinkFifo::SampleSinkFifo(int size) :
    m_total(0),
    m_writtenSignalCount(0),
    m_writtenSignalRateDivider(1),
    m_core(static_cast<unsigned int>(size))
{
    m_suppressed = -1;
}

SampleSinkFifo::SampleSinkFifo(const SampleSinkFifo& other) :
    m_total(0),
    m_writtenSignalCount(0),
    m_writtenSignalRateDivider(1),
    m_label(other.m_label),
    m_core(other.m_core)
{
    m_suppressed = -1;
}

SampleSinkFifo::~SampleSinkFifo() = default;

bool SampleSinkFifo::setSize(int size)
{
    m_core.setSize(static_cast<unsigned int>(size));
    return m_core.size() == static_cast<unsigned int>(size);
}

void SampleSinkFifo::setWrittenSignalRateDivider(unsigned int divider)
{
    m_writtenSignalRateDivider = divider;
}

unsigned int SampleSinkFifo::write(const quint8* data, unsigned int count)
{
    std::function<void()> dataReadyCallback;
    std::function<void(int)> overflowCallback;
    std::function<void(int, qint64)> writtenCallback;
    int writtenTotal = 0;
    qint64 writtenElapsed = 0;

    if (m_core.size() == 0) {
        return 0;
    }

    const Sample* begin = reinterpret_cast<const Sample*>(data);
    //count /= sizeof(Sample);
    count /= sizeof(Sample);
    const unsigned int total = m_core.write(begin, count);
    const bool notifyDataReady = total > 0;

    if (total < count)
    {
        if (m_suppressed < 0)
        {
            m_suppressed = 0;
            m_msgRateTimer = std::chrono::steady_clock::now();
            qCritical("SampleSinkFifo::write: (%s) overflow - dropping %u samples",
                qPrintable(m_label), count - total);
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                overflowCallback = m_overflowCallback;
            }
            if (overflowCallback) {
	            overflowCallback(static_cast<int>(count - total));
            }
        }
        else
        {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_msgRateTimer).count();

            if (elapsedMs > 2500)
            {
                qCritical("SampleSinkFifo::write: (%s) %u messages dropped", qPrintable(m_label), m_suppressed);
                qCritical("SampleSinkFifo::write: (%s) overflow - dropping %u samples",
                    qPrintable(m_label), count - total);
                {
                    std::lock_guard<std::mutex> lock(m_callbackMutex);
                    overflowCallback = m_overflowCallback;
                }
                if (overflowCallback) {
	                overflowCallback(static_cast<int>(count - total));
                }
                m_suppressed = -1;
            }
            else
            {
                m_suppressed++;
            }
        }
    }

    if (notifyDataReady) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        dataReadyCallback = m_dataReadyCallback;
    }

    if (notifyDataReady && dataReadyCallback) {
	    dataReadyCallback();
    }

    m_total += static_cast<int>(total);

    if (++m_writtenSignalCount >= m_writtenSignalRateDivider)
    {
        writtenTotal = m_total;
        writtenElapsed = MainCore::instance()->getElapsedNsecs();
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            writtenCallback = m_writtenCallback;
        }
        if (writtenCallback) {
	        writtenCallback(writtenTotal, writtenElapsed);
        }
        m_total = 0;
        m_writtenSignalCount = 0;
    }

    return total;
}

unsigned int SampleSinkFifo::write(SampleVector::const_iterator begin, SampleVector::const_iterator end)
{
    std::function<void()> dataReadyCallback;
    std::function<void(int)> overflowCallback;
    std::function<void(int, qint64)> writtenCallback;
    int writtenTotal = 0;
    qint64 writtenElapsed = 0;

    if (m_core.size() == 0) {
        return 0;
    }

    unsigned int count = static_cast<unsigned int>(end - begin);
    const unsigned int total = m_core.write(begin, end);
    const bool notifyDataReady = total > 0;

    if (total < count)
    {
        if (m_suppressed < 0)
        {
            m_suppressed = 0;
            m_msgRateTimer = std::chrono::steady_clock::now();
            qCritical("SampleSinkFifo::write: (%s) overflow - dropping %u samples",
                qPrintable(m_label), count - total);
            {
                std::lock_guard<std::mutex> lock(m_callbackMutex);
                overflowCallback = m_overflowCallback;
            }
            if (overflowCallback) {
	            overflowCallback(static_cast<int>(count - total));
            }
        }
        else
        {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_msgRateTimer).count();

            if (elapsedMs > 2500)
            {
                qCritical("SampleSinkFifo::write: (%s) %u messages dropped", qPrintable(m_label), m_suppressed);
                qCritical("SampleSinkFifo::write: (%s) overflow - dropping %u samples",
                    qPrintable(m_label), count - total);
                {
                    std::lock_guard<std::mutex> lock(m_callbackMutex);
                    overflowCallback = m_overflowCallback;
                }
                if (overflowCallback) {
	                overflowCallback(static_cast<int>(count - total));
                }
                m_suppressed = -1;
            }
            else
            {
                m_suppressed++;
            }
        }
    }

    if (notifyDataReady) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        dataReadyCallback = m_dataReadyCallback;
    }

    if (notifyDataReady && dataReadyCallback) {
	    dataReadyCallback();
    }

    m_total += static_cast<int>(total);

    if (++m_writtenSignalCount >= m_writtenSignalRateDivider)
    {
        writtenTotal = m_total;
        writtenElapsed = MainCore::instance()->getElapsedNsecs();
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            writtenCallback = m_writtenCallback;
        }
        if (writtenCallback) {
	        writtenCallback(writtenTotal, writtenElapsed);
        }
        m_total = 0;
        m_writtenSignalCount = 0;
    }

    return total;
}

unsigned int SampleSinkFifo::read(SampleVector::iterator begin, SampleVector::iterator end)
{
    std::function<void(int)> underflowCallback;

    if (m_core.size() == 0) {
        return 0;
    }

    unsigned int count = static_cast<unsigned int>(end - begin);
    const unsigned int total = m_core.read(begin, end);

    if (total < count)
    {
        qCritical("SampleSinkFifo::read: (%s) underflow - missing %u samples",
            qPrintable(m_label), count - total);
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            underflowCallback = m_underflowCallback;
        }
        if (underflowCallback) {
	        underflowCallback(static_cast<int>(count - total));
        }
    }

    return total;
}

unsigned int SampleSinkFifo::readBegin(unsigned int count,
    SampleVector::iterator* part1Begin, SampleVector::iterator* part1End,
    SampleVector::iterator* part2Begin, SampleVector::iterator* part2End)
{
    std::function<void(int)> underflowCallback;
    auto& data = m_core.data();

    if (m_core.size() == 0) {
        *part1Begin = data.end();
        *part1End = data.end();
        *part2Begin = data.end();
        *part2End = data.end();
        return 0;
    }

    SampleFifoSlices slices;
    const unsigned int total = m_core.readBegin(count, slices);

    if (total < count)
    {
        qCritical("SampleSinkFifo::readBegin: (%s) underflow - missing %u samples",
            qPrintable(m_label), count - total);
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            underflowCallback = m_underflowCallback;
        }
        if (underflowCallback) {
	        underflowCallback(static_cast<int>(count - total));
        }
    }

    if (slices.part1Begin == m_core.size()) {
        *part1Begin = data.end();
        *part1End = data.end();
    } else {
        *part1Begin = data.begin() + slices.part1Begin;
        *part1End = data.begin() + slices.part1End;
    }

    if (slices.part2Begin == m_core.size()) {
        *part2Begin = data.end();
        *part2End = data.end();
    } else {
        *part2Begin = data.begin() + slices.part2Begin;
        *part2End = data.begin() + slices.part2End;
    }

    return total;
}

unsigned int SampleSinkFifo::readCommit(unsigned int count)
{
    if (m_core.size() == 0) {
        return 0;
    }

    if (count > m_core.fill())
    {
        qCritical("SampleSinkFifo::readCommit: (%s) cannot commit more than available samples", qPrintable(m_label));
    }

    return m_core.readCommit(count);
}

unsigned int SampleSinkFifo::getSizePolicy(unsigned int sampleRate)
{
    return (sampleRate / 100) * 64; // .64s
}
