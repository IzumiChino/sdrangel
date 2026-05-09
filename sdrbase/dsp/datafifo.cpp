///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2016, 2018-2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com>    //
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

#include "datafifo.h"

void DataFifo::create(unsigned int s)
{
    m_size = 0;
    m_fill = 0;
    m_head = 0;
    m_tail = 0;

    m_data.resize(static_cast<int>(s));
    m_size = static_cast<unsigned int>(m_data.size());
}

void DataFifo::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_suppressed = -1;
    m_fill = 0;
    m_head = 0;
    m_tail = 0;
}

DataFifo::DataFifo() :
    m_suppressed(-1),
    m_currentDataType(DataTypeI16),
    m_size(0),
    m_fill(0),
    m_head(0),
    m_tail(0)
{
}

DataFifo::DataFifo(int size) :
    m_suppressed(-1),
    m_currentDataType(DataTypeI16),
    m_size(0),
    m_fill(0),
    m_head(0),
    m_tail(0)
{
    create(static_cast<unsigned int>(size));
}

DataFifo::DataFifo(const DataFifo& other) :
    m_suppressed(-1),
    m_data(other.m_data),
    m_currentDataType(DataTypeI16),
    m_size(static_cast<unsigned int>(other.m_data.size())),
    m_fill(0),
    m_head(0),
    m_tail(0)
{}

DataFifo::~DataFifo()
{}

bool DataFifo::setSize(int size)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    create(static_cast<unsigned int>(size));
    return static_cast<int>(m_data.size()) == size;
}

unsigned int DataFifo::write(const quint8* data, unsigned int count, DataType dataType)
{
    unsigned int total = 0;
    bool notifyDataReady = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_size == 0) {
            return 0;
        }

        if (dataType != m_currentDataType)
        {
            m_suppressed = -1;
            m_fill = 0;
            m_head = 0;
            m_tail = 0;
            m_currentDataType = dataType;
        }

        total = std::min(count, m_size - m_fill);

        if (total < count)
        {
            if (m_suppressed < 0)
            {
                m_suppressed = 0;
                m_msgRateTimer = std::chrono::steady_clock::now();
                qCritical("DataFifo::write: overflow - dropping %u bytes (size=%u)", count - total, m_size);
            }
            else
            {
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - m_msgRateTimer).count();
                if (elapsedMs > 2500)
                {
                    qCritical("DataFifo::write: %u messages dropped", m_suppressed);
                    qCritical("DataFifo::write: overflow - dropping %u bytes", count - total);
                    m_suppressed = -1;
                }
                else
                {
                    m_suppressed++;
                }
            }
        }

        unsigned int remaining = total;
        const quint8* begin = data;

        while (remaining > 0)
        {
            unsigned int len = std::min(remaining, m_size - m_tail);
            std::copy(begin, begin + len, m_data.begin() + m_tail);
            m_tail += len;
            m_tail %= m_size;
            m_fill += len;
            begin += len;
            remaining -= len;
        }

        notifyDataReady = m_fill > 0;
    }

    if (notifyDataReady && m_dataReadyCallback) {
        m_dataReadyCallback();
    }

    return total;
}

unsigned int DataFifo::write(QByteArray::const_iterator begin, QByteArray::const_iterator end, DataType dataType)
{
    unsigned int total = 0;
    bool notifyDataReady = false;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_size == 0 || begin == end) {
        return 0;
    }

    if (dataType != m_currentDataType)
    {
        m_suppressed = -1;
        m_fill = 0;
        m_head = 0;
        m_tail = 0;
        m_currentDataType = dataType;
    }

    unsigned int count = static_cast<unsigned int>(end - begin);
    total = std::min(count, m_size - m_fill);

    if (total < count)
    {
        if (m_suppressed < 0)
        {
            m_suppressed = 0;
            m_msgRateTimer = std::chrono::steady_clock::now();
            qCritical("DataFifo::write: overflow - dropping %u bytes (size=%u)", count - total, m_size);
        }
        else
        {
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_msgRateTimer).count();
            if (elapsedMs > 2500)
            {
                qCritical("DataFifo::write: %u messages dropped", m_suppressed);
                qCritical("DataFifo::write: overflow - dropping %u bytes", count - total);
                m_suppressed = -1;
            }
            else
            {
                m_suppressed++;
            }
        }
    }

    unsigned int remaining = total;

    while (remaining > 0)
    {
        unsigned int len = std::min(remaining, m_size - m_tail);
        std::copy(begin, begin + len, m_data.begin() + m_tail);
        m_tail += len;
        m_tail %= m_size;
        m_fill += len;
        begin += len;
        remaining -= len;
    }

    notifyDataReady = m_fill > 0;

    if (notifyDataReady && m_dataReadyCallback) {
        m_dataReadyCallback();
    }

    return total;
}

unsigned int DataFifo::read(QByteArray::iterator begin, QByteArray::iterator end, DataType& dataType)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    dataType = m_currentDataType;

    if (m_size == 0) {
        return 0;
    }

    unsigned int count = static_cast<unsigned int>(end - begin);
    unsigned int total = std::min(count, m_fill);

    if (total < count) {
        qCritical("DataFifo::read: underflow - missing %u bytes", count - total);
    }

    unsigned int remaining = total;

    while (remaining > 0)
    {
        unsigned int len = std::min(remaining, m_size - m_head);
        std::copy(m_data.begin() + m_head, m_data.begin() + m_head + len, begin);
        m_head += len;
        m_head %= m_size;
        m_fill -= len;
        begin += len;
        remaining -= len;
    }

    return total;
}

unsigned int DataFifo::readBegin(unsigned int count,
    QByteArray::iterator* part1Begin, QByteArray::iterator* part1End,
    QByteArray::iterator* part2Begin, QByteArray::iterator* part2End,
    DataType& dataType)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    dataType = m_currentDataType;

    if (m_size == 0)
    {
        *part1Begin = m_data.end();
        *part1End = m_data.end();
        *part2Begin = m_data.end();
        *part2End = m_data.end();
        return 0;
    }

    unsigned int total = std::min(count, m_fill);
    unsigned int remaining = total;
    unsigned int head = m_head;

    if (total < count) {
        qCritical("DataFifo::readBegin: underflow - missing %u bytes", count - total);
    }

    if (remaining > 0)
    {
        unsigned int len = std::min(remaining, m_size - head);
        *part1Begin = m_data.begin() + head;
        *part1End = m_data.begin() + head + len;
        head += len;
        head %= m_size;
        remaining -= len;
    }
    else
    {
        *part1Begin = m_data.end();
        *part1End = m_data.end();
    }

    if (remaining > 0)
    {
        unsigned int len = std::min(remaining, m_size - head);
        *part2Begin = m_data.begin() + head;
        *part2End = m_data.begin() + head + len;
    }
    else
    {
        *part2Begin = m_data.end();
        *part2End = m_data.end();
    }

    return total;
}

unsigned int DataFifo::readCommit(unsigned int count)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_size == 0) {
        return 0;
    }

    if (count > m_fill)
    {
        qCritical("DataFifo::readCommit: cannot commit more than available bytes");
        count = m_fill;
    }

    m_head = (m_head + count) % m_size;
    m_fill -= count;

    return count;
}
