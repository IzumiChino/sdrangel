///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2015-2020, 2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com>    //
// Copyright (C) 2023 Jon Beniston, M7RCE <jon@beniston.com>                     //
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

#ifndef INCLUDE_AUDIOFIFO_H
#define INCLUDE_AUDIOFIFO_H

#include <cstdint>
#include <functional>
#include <mutex>

#include <QString>
#include <QtGlobal>

#include "export.h"

class SDRBASE_API AudioFifo {
public:
	AudioFifo();
	AudioFifo(uint32_t numSamples);
	~AudioFifo();

	bool setSize(uint32_t numSamples);
    bool setSampleSize(uint32_t sampleSize, uint32_t numSamples);

	uint32_t write(const quint8* data, uint32_t numSamples);
	uint32_t read(quint8* data, uint32_t numSamples);
    bool readOne(quint8* data);
    uint32_t writeOne(const quint8* data);

	uint32_t drain(uint32_t numSamples);
	void clear();
    void setDataReadyCallback(std::function<void()> callback) { m_dataReadyCallback = std::move(callback); }
    void setOverflowCallback(std::function<void(int)> callback) { m_overflowCallback = std::move(callback); }
    void setUnderflowCallback(std::function<void()> callback) { m_underflowCallback = std::move(callback); }
	void notifyUnderflow() { if (m_underflowCallback) { m_underflowCallback(); } }

	inline uint32_t flush() { return drain(m_fill); }
	inline uint32_t fill() const { return m_fill; }
	inline bool isEmpty() const { return m_fill == 0; }
	inline bool isFull() const { return m_fill == m_size; }
	inline uint32_t size() const { return m_size; }
	void setLabel(const QString& label) { m_label = label; }

private:
	mutable std::mutex m_mutex;

	qint8* m_fifo;

	uint32_t m_sampleSize;

	uint32_t m_size;
	uint32_t m_fill;
	uint32_t m_head;
	uint32_t m_tail;
	QString m_label;
	std::function<void()> m_dataReadyCallback;
	std::function<void(int)> m_overflowCallback;
	std::function<void()> m_underflowCallback;

	bool create(uint32_t numSamples);
};

#endif // INCLUDE_AUDIOFIFO_H
