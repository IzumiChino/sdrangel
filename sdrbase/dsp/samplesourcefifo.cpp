///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2015-2019 Edouard Griffiths, F4EXB <f4exb06@gmail.com>          //
// Copyright (C) 2023 Daniele Forsi <iu5hkx@gmail.com>                           //
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

#include "samplesourcefifo.h"

const unsigned int SampleSourceFifo::m_rwDivisor = SampleSourceFifoCore::kRwDivisor;
const unsigned int SampleSourceFifo::m_guardDivisor = SampleSourceFifoCore::kGuardDivisor;

SampleSourceFifo::SampleSourceFifo() = default;

SampleSourceFifo::SampleSourceFifo(unsigned int size) :
    m_core(size)
{}

void SampleSourceFifo::resize(unsigned int size)
{
    m_core.setSize(size);
}

void SampleSourceFifo::reset()
{
    m_core.reset();
}

SampleSourceFifo::~SampleSourceFifo() = default;

void SampleSourceFifo::read(
    unsigned int amount,
    unsigned int& ipart1Begin, unsigned int& ipart1End,
    unsigned int& ipart2Begin, unsigned int& ipart2End)
{
    std::function<void()> dataReadCallback;
    SampleFifoSlices slices;
    m_core.read(amount, slices);
    ipart1Begin = slices.part1Begin;
    ipart1End = slices.part1End;
    ipart2Begin = slices.part2Begin;
    ipart2End = slices.part2End;

    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        dataReadCallback = m_dataReadCallback;
    }

    if (dataReadCallback) {
	    dataReadCallback();
    }
}

void SampleSourceFifo::write(
    unsigned int amount,
    unsigned int& ipart1Begin, unsigned int& ipart1End,
    unsigned int& ipart2Begin, unsigned int& ipart2End)
{
    SampleFifoSlices slices;
    const auto correction = m_core.write(amount, slices);

    if (correction == SampleSourceFifoCore::WriteCorrection::Underrun)
    {
        qWarning("SampleSourceFifo::write: underrun (write too slow) using %d old samples",
            static_cast<int>(m_core.midPoint() - m_core.lowGuard()));
    }
    else if (correction == SampleSourceFifoCore::WriteCorrection::Overrun)
    {
        qWarning("SampleSourceFifo::write: overrun (read too slow) dropping %d samples",
            static_cast<int>(m_core.highGuard() - m_core.midPoint()));
    }

    ipart1Begin = slices.part1Begin;
    ipart1End = slices.part1End;
    ipart2Begin = slices.part2Begin;
    ipart2End = slices.part2End;
}

unsigned int SampleSourceFifo::getSizePolicy(unsigned int sampleRate)
{
    return (sampleRate / 100) * 64; // 0.64 s
}
