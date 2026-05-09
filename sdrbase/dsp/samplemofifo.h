///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2019 Edouard Griffiths, F4EXB <f4exb06@gmail.com>               //
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

#ifndef INCLUDE_SAMPLEMOFIFO_H
#define INCLUDE_SAMPLEMOFIFO_H

#include <functional>
#include <mutex>

#include <QtGlobal>

#include "dsp/dsptypes.h"
#include "export.h"

class SDRBASE_API SampleMOFifo {
public:
    SampleMOFifo();
    SampleMOFifo(unsigned int nbStreams, unsigned int size);
    ~SampleMOFifo();

    void init(unsigned int nbStreams, unsigned int size);
    void resize(unsigned int size);
    void reset();

    void readSync(
        unsigned int amount,
		unsigned int& ipart1Begin, unsigned int& ipart1End, // first part offsets where to read
		unsigned int& ipart2Begin, unsigned int& ipart2End  // second part offsets
    );

    void writeSync( //!< in place write with given amount
        unsigned int amount,
		unsigned int& ipart1Begin, unsigned int& ipart1End, // first part offsets where to write
		unsigned int& ipart2Begin, unsigned int& ipart2End  // second part offsets
    );

    void readAsync(
        unsigned int amount,
		unsigned int& ipart1Begin, unsigned int& ipart1End,
		unsigned int& ipart2Begin, unsigned int& ipart2End,
        unsigned int stream
    );

    void writeAsync( //!< in place write with given amount
        unsigned int amount,
		unsigned int& ipart1Begin, unsigned int& ipart1End,
		unsigned int& ipart2Begin, unsigned int& ipart2End,
        unsigned int stream
    );

    std::vector<SampleVector>& getData() { return m_data; }
    SampleVector& getData(unsigned int stream) { return m_data[stream]; }
    unsigned int getNbStreams() const { return m_data.size(); }

    unsigned int remainderSync()
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_readCount;
    }

    unsigned int remainderAsync(unsigned int stream)
    {
        if (stream >= m_nbStreams) {
            return 0;
        }

        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_vReadCount[stream];
    }

    static unsigned int getSizePolicy(unsigned int sampleRate);
    static const unsigned int m_rwDivisor;
    static const unsigned int m_guardDivisor;
    void setDataReadSyncCallback(std::function<void()> callback) { m_dataReadSyncCallback = std::move(callback); }
    void setDataReadAsyncCallback(std::function<void(int)> callback) { m_dataReadAsyncCallback = std::move(callback); }

private:
    std::vector<SampleVector> m_data;
    unsigned int m_nbStreams;
    unsigned int m_size;
    unsigned int m_lowGuard;
    unsigned int m_highGuard;
    unsigned int m_midPoint;
    unsigned int m_readCount;
    unsigned int m_readHead;
    unsigned int m_writeHead;
    std::vector<unsigned int> m_vReadCount;
    std::vector<unsigned int> m_vReadHead;
    std::vector<unsigned int> m_vWriteHead;
    std::function<void()> m_dataReadSyncCallback;
    std::function<void(int)> m_dataReadAsyncCallback;
    mutable std::recursive_mutex m_mutex;
};

#endif // INCLUDE_SAMPLEMOFIFO_H
