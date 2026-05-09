///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2015-2016, 2018-2021 Edouard Griffiths, F4EXB <f4exb06@gmail.com> //
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

#ifndef INCLUDE_DATAFIFO_H
#define INCLUDE_DATAFIFO_H

#include <QByteArray>
#include <functional>
#include <mutex>
#include <chrono>

#include "export.h"

class QObject;

class SDRBASE_API DataFifo {
public:
	enum DataType
	{
		DataTypeI16,  //!< 16 bit signed integer
		DataTypeCI16  //!< Complex (i.e. Re, Im pair of) 16 bit signed integer
	};

	DataFifo();
	explicit DataFifo(int size);
    DataFifo(const DataFifo& other);
	~DataFifo();

	bool setSize(int size);
    void reset();
    void setDataReadyCallback(std::function<void()> callback) { m_dataReadyCallback = std::move(callback); }
	inline unsigned int size() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_size;
	}
	inline unsigned int fill() {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_fill;
	}

	unsigned int write(const quint8* data, unsigned int count, DataType dataType);
	unsigned int write(QByteArray::const_iterator begin, QByteArray::const_iterator end, DataType dataType);

	unsigned int read(QByteArray::iterator begin, QByteArray::iterator end, DataType& dataType);

	unsigned int readBegin(unsigned int count,
		QByteArray::iterator* part1Begin, QByteArray::iterator* part1End,
		QByteArray::iterator* part2Begin, QByteArray::iterator* part2End,
		DataType& dataType);
	unsigned int readCommit(unsigned int count);

private:
	mutable std::mutex m_mutex;
	std::chrono::steady_clock::time_point m_msgRateTimer;
	int m_suppressed;
	QByteArray m_data;
	std::function<void()> m_dataReadyCallback;
	DataType m_currentDataType;

	unsigned int m_size;
	unsigned int m_fill;
	unsigned int m_head;
	unsigned int m_tail;

	void create(unsigned int s);
};

SDRBASE_API DataFifo* getDataFifoFromPipeElement(QObject *element);
SDRBASE_API const DataFifo* getDataFifoFromPipeElement(const QObject *element);

#endif // INCLUDE_DATAFIFO_H
