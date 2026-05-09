///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2015-2016, 2018-2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com> //
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

#ifndef INCLUDE_SAMPLEFIFO_H
#define INCLUDE_SAMPLEFIFO_H

#include <chrono>
#include <functional>
#include <mutex>

#include <QString>
#include <QtGlobal>

#include "samplefifocore.h"
#include "export.h"

class SDRBASE_API SampleSinkFifo {
private:
	std::chrono::steady_clock::time_point m_msgRateTimer;
	int m_suppressed;
	int m_total;
	unsigned int m_writtenSignalCount;
	unsigned int m_writtenSignalRateDivider;
	QString m_label;
	SampleSinkFifoCore m_core;
    std::function<void()> m_dataReadyCallback;
    std::function<void(int)> m_overflowCallback;
    std::function<void(int)> m_underflowCallback;
    std::function<void(int, qint64)> m_writtenCallback;
	mutable std::mutex m_callbackMutex;

public:
	SampleSinkFifo();
	explicit SampleSinkFifo(int size);
    SampleSinkFifo(const SampleSinkFifo& other);
	~SampleSinkFifo();

	bool setSize(int size);
    void reset();
	void setWrittenSignalRateDivider(unsigned int divider);
	void setDataReadyCallback(std::function<void()> callback) {
		std::lock_guard<std::mutex> lock(m_callbackMutex);
		m_dataReadyCallback = std::move(callback);
	}
	void setOverflowCallback(std::function<void(int)> callback) {
		std::lock_guard<std::mutex> lock(m_callbackMutex);
		m_overflowCallback = std::move(callback);
	}
	void setUnderflowCallback(std::function<void(int)> callback) {
		std::lock_guard<std::mutex> lock(m_callbackMutex);
		m_underflowCallback = std::move(callback);
	}
	void setWrittenCallback(std::function<void(int, qint64)> callback) {
		std::lock_guard<std::mutex> lock(m_callbackMutex);
		m_writtenCallback = std::move(callback);
	}

	inline unsigned int size() {
		return m_core.size();
	}
	inline unsigned int fill() {
		return m_core.fill();
	}

	unsigned int write(const quint8* data, unsigned int count);
	unsigned int write(SampleVector::const_iterator begin, SampleVector::const_iterator end);
	unsigned int read(SampleVector::iterator begin, SampleVector::iterator end);
	unsigned int readBegin(unsigned int count,
		SampleVector::iterator* part1Begin, SampleVector::iterator* part1End,
		SampleVector::iterator* part2Begin, SampleVector::iterator* part2End);
	unsigned int readCommit(unsigned int count);

	void setLabel(const QString& label) { m_label = label; }
    const QString& getLabel() const { return m_label; }
    static unsigned int getSizePolicy(unsigned int sampleRate);
};

#endif // INCLUDE_SAMPLEFIFO_H
