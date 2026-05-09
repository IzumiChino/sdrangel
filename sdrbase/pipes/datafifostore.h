///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2015-2016, 2018-2019, 2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com> //
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

#ifndef SDRBASE_PIPES_DATAFIFOSTORE_H_
#define SDRBASE_PIPES_DATAFIFOSTORE_H_

#include <QList>
#include <QObject>

#include "dsp/datafifo.h"
#include "export.h"
#include "objectpipeelementsstore.h"

class SDRBASE_API DataPipeElement : public QObject
{
public:
    DataPipeElement();
    ~DataPipeElement() override = default;

    DataFifo *getFifo() { return &m_fifo; }
    const DataFifo *getFifo() const { return &m_fifo; }

private:
    DataFifo m_fifo;
};

class SDRBASE_API DataFifoStore : public ObjectPipeElementsStore
{
public:
    DataFifoStore();
    virtual ~DataFifoStore();

    virtual QObject *createElement();
    virtual void deleteElement(QObject*);

private:
    void deleteAllElements();
    QList<DataPipeElement*> m_dataFifos;
};

#endif // SDRBASE_PIPES_DATAFIFOSTORE_H_
