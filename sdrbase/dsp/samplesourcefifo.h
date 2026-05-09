///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2014 John Greb <hexameron@spam.no>                              //
// Copyright (C) 2015-2016, 2018-2019, 2021 Edouard Griffiths, F4EXB <f4exb06@gmail.com> //
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

#ifndef SDRBASE_DSP_SAMPLESOURCEFIFO_H_
#define SDRBASE_DSP_SAMPLESOURCEFIFO_H_

#include <functional>
#include <mutex>

#include "samplefifocore.h"
#include "export.h"

class SDRBASE_API SampleSourceFifo {
public:
    SampleSourceFifo();
    explicit SampleSourceFifo(unsigned int size);
    ~SampleSourceFifo();
    void resize(unsigned int size);
    void reset();
    void setDataReadCallback(std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_dataReadCallback = std::move(callback);
    }

    SampleVector& getData() { return m_core.data(); }
    void read(
        unsigned int amount,
        unsigned int& ipart1Begin, unsigned int& ipart1End, // first part offsets where to read
        unsigned int& ipart2Begin, unsigned int& ipart2End  // second part offsets
    );
    void write(
        unsigned int amount,
        unsigned int& ipart1Begin, unsigned int& ipart1End, // first part offsets where to write
        unsigned int& ipart2Begin, unsigned int& ipart2End  // second part offsets
    );
    unsigned int remainder()
    {
        return m_core.remainder();
    }
    /** returns ratio of off center over buffer size with sign: negative read lags and positive read leads */
    float getRWBalance() const
    {
        return m_core.getRWBalance();
    }
    unsigned int size() const {
        return m_core.size();
    }

    static unsigned int getSizePolicy(unsigned int sampleRate);
    static const unsigned int m_rwDivisor;
    static const unsigned int m_guardDivisor;

private:
    SampleSourceFifoCore m_core;
    std::function<void()> m_dataReadCallback;
    mutable std::mutex m_callbackMutex;
};

#endif // SDRBASE_DSP_SAMPLESOURCEFIFO_H_
