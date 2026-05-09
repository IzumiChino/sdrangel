///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2012 maintech GmbH, Otto-Hahn-Str. 15, 97204 Hoechberg, Germany //
// written by Christian Daniel                                                   //
// Copyright (C) 2015-2019, 2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com>    //
// Copyright (C) 2021-2022 Jon Beniston, M7RCE <jon@beniston.com>                //
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

#include "dsp/datafifo.h"
#include "datafifostore.h"

DataPipeElement::DataPipeElement()
{
    setObjectName("DataFifo");
}

DataFifoStore::DataFifoStore()
{}

DataFifoStore::~DataFifoStore()
{
    deleteAllElements();
}

QObject *DataFifoStore::createElement()
{
    auto *fifoElement = new DataPipeElement();
    m_dataFifos.push_back(fifoElement);
    qDebug("DataFifoStore::createElement: %d added", (int)m_dataFifos.size() - 1);
    return fifoElement;
}

void DataFifoStore::deleteElement(QObject *element)
{
    auto *fifoElement = dynamic_cast<DataPipeElement*>(element);

    if (!fifoElement) {
        return;
    }

    int i = m_dataFifos.indexOf(fifoElement);

    if (i >= 0)
    {
        qDebug("DataFifoStore::deleteElement: delete element at %d", i);
        delete m_dataFifos[i];
        m_dataFifos.removeAt(i);
    }
}

void DataFifoStore::deleteAllElements()
{
    for (auto& fifo : m_dataFifos) {
        delete fifo;
    }

    m_dataFifos.clear();
}

DataFifo* getDataFifoFromPipeElement(QObject *element)
{
    auto *fifoElement = dynamic_cast<DataPipeElement*>(element);
    return fifoElement ? fifoElement->getFifo() : nullptr;
}

const DataFifo* getDataFifoFromPipeElement(const QObject *element)
{
    auto *fifoElement = dynamic_cast<const DataPipeElement*>(element);
    return fifoElement ? fifoElement->getFifo() : nullptr;
}
