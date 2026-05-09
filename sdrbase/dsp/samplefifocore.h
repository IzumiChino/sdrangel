#ifndef SDRBASE_DSP_SAMPLEFIFOCORE_H_
#define SDRBASE_DSP_SAMPLEFIFOCORE_H_

#include <algorithm>
#include <atomic>
#include <mutex>

#include "dsp/dsptypes.h"

struct SampleFifoSlices
{
    unsigned int part1Begin = 0;
    unsigned int part1End = 0;
    unsigned int part2Begin = 0;
    unsigned int part2End = 0;
};

class SampleSinkFifoCore
{
public:
    SampleSinkFifoCore() = default;

    explicit SampleSinkFifoCore(unsigned int size)
    {
        setSize(size);
    }

    SampleSinkFifoCore(const SampleSinkFifoCore& other)
    {
        std::lock_guard<std::mutex> lock(other.m_adminMutex);
        m_data = other.m_data;
        m_size = other.m_size;
        m_tail.store(other.m_tail.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_head.store(other.m_head.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_readBeginHead = other.m_readBeginHead;
    }

    void setSize(unsigned int size)
    {
        std::lock_guard<std::mutex> lock(m_adminMutex);
        m_data.resize(size);
        m_size = static_cast<unsigned int>(m_data.size());
        m_tail.store(0, std::memory_order_relaxed);
        m_head.store(0, std::memory_order_relaxed);
        m_readBeginHead = 0;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(m_adminMutex);
        m_tail.store(0, std::memory_order_relaxed);
        m_head.store(0, std::memory_order_relaxed);
        m_readBeginHead = 0;
    }

    unsigned int size() const
    {
        std::lock_guard<std::mutex> lock(m_adminMutex);
        return m_size;
    }

    unsigned int fill() const
    {
        const unsigned int tail = m_tail.load(std::memory_order_acquire);
        const unsigned int head = m_head.load(std::memory_order_acquire);
        return tail - head;
    }

    SampleVector& data()
    {
        return m_data;
    }

    const SampleVector& data() const
    {
        return m_data;
    }

    unsigned int write(const Sample* begin, unsigned int count)
    {
        if (m_size == 0) {
            return 0;
        }

        const unsigned int head = m_head.load(std::memory_order_acquire);
        const unsigned int tail = m_tail.load(std::memory_order_relaxed);
        const unsigned int currentFill = tail - head;
        const unsigned int space = m_size - currentFill;
        const unsigned int total = std::min(count, space);

        if (total == 0) {
            return 0;
        }

        const unsigned int pos = tail % m_size;
        const unsigned int toEnd = m_size - pos;

        if (total <= toEnd)
        {
            std::copy(begin, begin + total, m_data.begin() + pos);
        }
        else
        {
            std::copy(begin, begin + toEnd, m_data.begin() + pos);
            std::copy(begin + toEnd, begin + total, m_data.begin());
        }

        m_tail.store(tail + total, std::memory_order_release);
        return total;
    }

    unsigned int write(SampleVector::const_iterator begin, SampleVector::const_iterator end)
    {
        if (m_size == 0) {
            return 0;
        }

        const unsigned int count = static_cast<unsigned int>(end - begin);
        const unsigned int head = m_head.load(std::memory_order_acquire);
        const unsigned int tail = m_tail.load(std::memory_order_relaxed);
        const unsigned int currentFill = tail - head;
        const unsigned int space = m_size - currentFill;
        const unsigned int total = std::min(count, space);

        if (total == 0) {
            return 0;
        }

        const unsigned int pos = tail % m_size;
        const unsigned int toEnd = m_size - pos;

        if (total <= toEnd)
        {
            std::copy(begin, begin + total, m_data.begin() + pos);
        }
        else
        {
            std::copy(begin, begin + toEnd, m_data.begin() + pos);
            std::copy(begin + toEnd, begin + total, m_data.begin());
        }

        m_tail.store(tail + total, std::memory_order_release);
        return total;
    }

    unsigned int read(SampleVector::iterator begin, SampleVector::iterator end)
    {
        if (m_size == 0) {
            return 0;
        }

        const unsigned int count = static_cast<unsigned int>(end - begin);
        const unsigned int tail = m_tail.load(std::memory_order_acquire);
        const unsigned int head = m_head.load(std::memory_order_relaxed);
        const unsigned int currentFill = tail - head;
        const unsigned int total = std::min(count, currentFill);

        if (total == 0) {
            return 0;
        }

        const unsigned int pos = head % m_size;
        const unsigned int toEnd = m_size - pos;

        if (total <= toEnd)
        {
            std::copy(m_data.begin() + pos, m_data.begin() + pos + total, begin);
        }
        else
        {
            std::copy(m_data.begin() + pos, m_data.end(), begin);
            std::copy(m_data.begin(), m_data.begin() + (total - toEnd), begin + toEnd);
        }

        m_head.store(head + total, std::memory_order_release);
        return total;
    }

    unsigned int readBegin(unsigned int count, SampleFifoSlices& slices)
    {
        if (m_size == 0)
        {
            slices.part1Begin = m_size;
            slices.part1End = m_size;
            slices.part2Begin = m_size;
            slices.part2End = m_size;
            return 0;
        }

        const unsigned int tail = m_tail.load(std::memory_order_acquire);
        const unsigned int head = m_head.load(std::memory_order_relaxed);
        const unsigned int currentFill = tail - head;
        const unsigned int total = std::min(count, currentFill);
        unsigned int pos = head % m_size;
        unsigned int remaining = total;

        m_readBeginHead = head;
        slices.part1Begin = m_size;
        slices.part1End = m_size;
        slices.part2Begin = m_size;
        slices.part2End = m_size;

        if (remaining > 0)
        {
            const unsigned int len = std::min(remaining, m_size - pos);
            slices.part1Begin = pos;
            slices.part1End = pos + len;
            pos = (pos + len) % m_size;
            remaining -= len;
        }

        if (remaining > 0)
        {
            slices.part2Begin = pos;
            slices.part2End = pos + remaining;
        }

        return total;
    }

    unsigned int readCommit(unsigned int count)
    {
        if (m_size == 0) {
            return 0;
        }

        const unsigned int tail = m_tail.load(std::memory_order_acquire);
        const unsigned int head = m_readBeginHead;
        const unsigned int currentFill = tail - head;

        if (count > currentFill) {
            count = currentFill;
        }

        m_head.store(head + count, std::memory_order_release);
        return count;
    }

private:
    mutable std::mutex m_adminMutex;
    SampleVector m_data;
    unsigned int m_size = 0;
    std::atomic<unsigned int> m_tail{0};
    std::atomic<unsigned int> m_head{0};
    unsigned int m_readBeginHead = 0;
};

class SampleSourceFifoCore
{
public:
    enum class WriteCorrection
    {
        None,
        Underrun,
        Overrun
    };

    static constexpr unsigned int kRwDivisor = 2;
    static constexpr unsigned int kGuardDivisor = 10;

    SampleSourceFifoCore() = default;

    explicit SampleSourceFifoCore(unsigned int size)
    {
        setSize(size);
    }

    void setSize(unsigned int size)
    {
        std::lock_guard<std::mutex> lock(m_adminMutex);
        m_size = size;
        m_lowGuard = m_size / kGuardDivisor;
        m_highGuard = m_size - (m_size / kGuardDivisor);
        m_midPoint = m_size / kRwDivisor;
        m_readCount.store(0, std::memory_order_relaxed);
        m_readHead.store(0, std::memory_order_relaxed);
        m_writeHead.store(m_midPoint, std::memory_order_relaxed);
        m_data.resize(size);
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(m_adminMutex);
        m_readCount.store(0, std::memory_order_relaxed);
        m_readHead.store(0, std::memory_order_relaxed);
        m_writeHead.store(m_midPoint, std::memory_order_relaxed);
    }

    SampleVector& data()
    {
        return m_data;
    }

    const SampleVector& data() const
    {
        return m_data;
    }

    unsigned int remainder() const
    {
        return m_readCount.load(std::memory_order_acquire);
    }

    float getRWBalance() const
    {
        if (m_size == 0) {
            return 0.0f;
        }

        const unsigned int writeHead = m_writeHead.load(std::memory_order_acquire);
        const unsigned int readHead = m_readHead.load(std::memory_order_acquire);
        int delta;
        if (writeHead > readHead) {
            delta = static_cast<int>(m_size / kRwDivisor) - static_cast<int>(writeHead - readHead);
        } else {
            delta = static_cast<int>(readHead - writeHead) - static_cast<int>(m_size / kRwDivisor);
        }

        return delta / static_cast<float>(m_size);
    }

    unsigned int size() const
    {
        std::lock_guard<std::mutex> lock(m_adminMutex);
        return m_size;
    }

    unsigned int lowGuard() const
    {
        std::lock_guard<std::mutex> lock(m_adminMutex);
        return m_lowGuard;
    }

    unsigned int highGuard() const
    {
        std::lock_guard<std::mutex> lock(m_adminMutex);
        return m_highGuard;
    }

    unsigned int midPoint() const
    {
        std::lock_guard<std::mutex> lock(m_adminMutex);
        return m_midPoint;
    }

    void read(unsigned int amount, SampleFifoSlices& slices)
    {
        if (m_size == 0)
        {
            slices.part1Begin = 0;
            slices.part1End = 0;
            slices.part2Begin = 0;
            slices.part2End = 0;
            return;
        }

        unsigned int readHead = m_readHead.load(std::memory_order_relaxed);
        const unsigned int spaceLeft = m_size - readHead;
        const unsigned int oldCount = m_readCount.load(std::memory_order_relaxed);
        const unsigned int newCount = oldCount + amount < m_size ? oldCount + amount : m_size;
        m_readCount.store(newCount, std::memory_order_release);

        if (amount <= spaceLeft)
        {
            slices.part1Begin = readHead;
            slices.part1End = readHead + amount;
            slices.part2Begin = m_size;
            slices.part2End = m_size;
            readHead += amount;
        }
        else
        {
            const unsigned int remaining = (amount < m_size ? amount : m_size) - spaceLeft;
            slices.part1Begin = readHead;
            slices.part1End = m_size;
            slices.part2Begin = 0;
            slices.part2End = remaining;
            readHead = remaining;
        }

        m_readHead.store(readHead, std::memory_order_release);
    }

    WriteCorrection write(unsigned int amount, SampleFifoSlices& slices)
    {
        if (m_size == 0)
        {
            slices.part1Begin = 0;
            slices.part1End = 0;
            slices.part2Begin = 0;
            slices.part2End = 0;
            return WriteCorrection::None;
        }

        const unsigned int readHead = m_readHead.load(std::memory_order_acquire);
        unsigned int writeHead = m_writeHead.load(std::memory_order_relaxed);
        const unsigned int rwDelta = writeHead >= readHead ?
            writeHead - readHead :
            m_size - (readHead - writeHead);

        auto correction = WriteCorrection::None;

        if (rwDelta < m_lowGuard)
        {
            writeHead = readHead + m_midPoint < m_size ?
                readHead + m_midPoint :
                readHead + m_midPoint - m_size;
            correction = WriteCorrection::Underrun;
        }
        else if (rwDelta > m_highGuard)
        {
            writeHead = readHead + m_midPoint < m_size ?
                readHead + m_midPoint :
                readHead + m_midPoint - m_size;
            correction = WriteCorrection::Overrun;
        }

        const unsigned int spaceLeft = m_size - writeHead;

        if (amount <= spaceLeft)
        {
            slices.part1Begin = writeHead;
            slices.part1End = writeHead + amount;
            slices.part2Begin = m_size;
            slices.part2End = m_size;
            writeHead += amount;
        }
        else
        {
            const unsigned int remaining = (amount < m_size ? amount : m_size) - spaceLeft;
            slices.part1Begin = writeHead;
            slices.part1End = m_size;
            slices.part2Begin = 0;
            slices.part2End = remaining;
            writeHead = remaining;
        }

        const unsigned int readCount = m_readCount.load(std::memory_order_acquire);
        m_readCount.store(amount < readCount ? readCount - amount : 0, std::memory_order_release);
        m_writeHead.store(writeHead, std::memory_order_release);
        return correction;
    }

private:
    mutable std::mutex m_adminMutex;
    SampleVector m_data;
    unsigned int m_size = 0;
    unsigned int m_lowGuard = 0;
    unsigned int m_highGuard = 0;
    unsigned int m_midPoint = 0;
    std::atomic<unsigned int> m_readHead{0};
    std::atomic<unsigned int> m_writeHead{0};
    std::atomic<unsigned int> m_readCount{0};
};

#endif // SDRBASE_DSP_SAMPLEFIFOCORE_H_