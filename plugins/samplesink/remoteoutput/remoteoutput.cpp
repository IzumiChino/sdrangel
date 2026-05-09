///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2022 Edouard Griffiths, F4EXB <f4exb06@gmail.com>          //
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

#include <string.h>
#include <errno.h>
#include <QDebug>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonParseError>
#include <QBuffer>

#include "SWGDeviceSettings.h"
#include "SWGDeviceState.h"
#include "SWGDeviceReport.h"
#include "SWGRemoteOutputReport.h"

#include "util/simpleserializer.h"
#include "util/timeutil.h"
#include "dsp/dspcommands.h"

#include "device/deviceapi.h"

#include "remoteoutput.h"
#include "remoteoutputworker.h"

MESSAGE_CLASS_DEFINITION(RemoteOutput::MsgConfigureRemoteOutput, Message)
MESSAGE_CLASS_DEFINITION(RemoteOutput::MsgConfigureRemoteOutputWork, Message)
MESSAGE_CLASS_DEFINITION(RemoteOutput::MsgStartStop, Message)
MESSAGE_CLASS_DEFINITION(RemoteOutput::MsgConfigureRemoteOutputChunkCorrection, Message)
MESSAGE_CLASS_DEFINITION(RemoteOutput::MsgReportRemoteData, Message)
MESSAGE_CLASS_DEFINITION(RemoteOutput::MsgReportRemoteFixedData, Message)
MESSAGE_CLASS_DEFINITION(RemoteOutput::MsgReportRemoteStatus, Message)
MESSAGE_CLASS_DEFINITION(RemoteOutput::MsgRequestFixedData, Message)

RemoteOutput::RemoteOutput(DeviceAPI *deviceAPI) :
    m_deviceAPI(deviceAPI),
	m_settings(),
	m_masterTimer(deviceAPI->getMasterTimer())
{
    m_deviceAPI->setNbSinkStreams(1);
    m_networkManager = new QNetworkAccessManager();
    QObject::connect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &RemoteOutput::networkManagerFinished
    );
    connect(&m_masterTimer, SIGNAL(timeout()), this, SLOT(tick()));
    applyCenterFrequency();
    applySampleRate();
}

RemoteOutput::~RemoteOutput()
{
    QObject::disconnect(
        m_networkManager,
        &QNetworkAccessManager::finished,
        this,
        &RemoteOutput::networkManagerFinished
    );
	RemoteOutput::stop();
	delete m_networkManager;
}

void RemoteOutput::destroy()
{
    delete this;
}

bool RemoteOutput::start()
{
	QMutexLocker mutexLocker(&m_mutex);

    if (m_running) {
        return true;
    }

	qDebug() << "RemoteOutput::start";

	m_remoteOutputWorker = new RemoteOutputWorker(&m_sampleSourceFifo);
    m_remoteOutputWorker->moveToThread(&m_remoteOutputWorkerThread);
    QObject::connect(&m_remoteOutputWorkerThread, &QThread::finished, m_remoteOutputWorker, &QObject::deleteLater);
    m_remoteOutputWorker->setDeviceIndex(m_deviceAPI->getDeviceSetIndex());
	m_remoteOutputWorker->setDataAddress(m_settings.m_dataAddress, m_settings.m_dataPort);
	m_remoteOutputWorker->setSamplerate(m_sampleRate);
	m_remoteOutputWorker->setNbBlocksFEC(m_settings.m_nbFECBlocks);
	m_remoteOutputWorker->connectTimer(m_masterTimer);
	startWorker();
    m_running = true;

	mutexLocker.unlock();
    applySampleRate();

	qDebug("RemoteOutput::start: started");

	return true;
}

void RemoteOutput::init()
{
    applySettings(m_settings, QList<QString>(), true);
}

void RemoteOutput::stop()
{
	QMutexLocker mutexLocker(&m_mutex);

    if (!m_running) {
        return;
    }

	qDebug() << "RemoteOutput::stop";
    m_running = false;

	if (m_remoteOutputWorker)
	{
        stopWorker();
		m_remoteOutputWorker = nullptr;
	}
}

void RemoteOutput::startWorker()
{
    if (!m_remoteOutputWorker) {
        return;
    }

    if (!m_remoteOutputWorkerThread.isRunning()) {
        m_remoteOutputWorkerThread.start();
    }

    QMetaObject::invokeMethod(m_remoteOutputWorker, "startWork", Qt::BlockingQueuedConnection);
}

void RemoteOutput::stopWorker()
{
    if (!m_remoteOutputWorker || !m_remoteOutputWorkerThread.isRunning()) {
        return;
    }

    QMetaObject::invokeMethod(m_remoteOutputWorker, "stopWork", Qt::BlockingQueuedConnection);
	m_remoteOutputWorkerThread.quit();
	m_remoteOutputWorkerThread.wait();
}

void RemoteOutput::setWorkerDataAddress(const QString& address, uint16_t port)
{
    if (!m_remoteOutputWorker) {
        return;
    }

    if (m_remoteOutputWorkerThread.isRunning())
    {
        RemoteOutputWorker *worker = m_remoteOutputWorker;
        QMetaObject::invokeMethod(worker, [worker, address, port]() {
            worker->setDataAddress(address, port);
        }, Qt::BlockingQueuedConnection);
    }
    else
    {
        m_remoteOutputWorker->setDataAddress(address, port);
    }
}

void RemoteOutput::setWorkerNbBlocksFEC(uint32_t nbBlocksFEC)
{
    if (!m_remoteOutputWorker) {
        return;
    }

    if (m_remoteOutputWorkerThread.isRunning())
    {
        RemoteOutputWorker *worker = m_remoteOutputWorker;
        QMetaObject::invokeMethod(worker, [worker, nbBlocksFEC]() {
            worker->setNbBlocksFEC(nbBlocksFEC);
        }, Qt::BlockingQueuedConnection);
    }
    else
    {
        m_remoteOutputWorker->setNbBlocksFEC(nbBlocksFEC);
    }
}

void RemoteOutput::setWorkerNbTxBytes(uint32_t nbTxBytes)
{
    if (!m_remoteOutputWorker) {
        return;
    }

    if (m_remoteOutputWorkerThread.isRunning())
    {
        RemoteOutputWorker *worker = m_remoteOutputWorker;
        QMetaObject::invokeMethod(worker, [worker, nbTxBytes]() {
            worker->setNbTxBytes(nbTxBytes);
        }, Qt::BlockingQueuedConnection);
    }
    else
    {
        m_remoteOutputWorker->setNbTxBytes(nbTxBytes);
    }
}

void RemoteOutput::setWorkerSampleRate(int sampleRate)
{
    if (!m_remoteOutputWorker) {
        return;
    }

    if (m_remoteOutputWorkerThread.isRunning())
    {
        RemoteOutputWorker *worker = m_remoteOutputWorker;
        QMetaObject::invokeMethod(worker, [worker, sampleRate]() {
            worker->setSamplerate(sampleRate);
        }, Qt::BlockingQueuedConnection);
    }
    else
    {
        m_remoteOutputWorker->setSamplerate(sampleRate);
    }
}

void RemoteOutput::setWorkerChunkCorrection(int chunkCorrection)
{
    if (!m_remoteOutputWorker) {
        return;
    }

    if (m_remoteOutputWorkerThread.isRunning())
    {
        RemoteOutputWorker *worker = m_remoteOutputWorker;
        QMetaObject::invokeMethod(worker, [worker, chunkCorrection]() {
            worker->setChunkCorrection(chunkCorrection);
        }, Qt::BlockingQueuedConnection);
    }
    else
    {
        m_remoteOutputWorker->setChunkCorrection(chunkCorrection);
    }
}

QByteArray RemoteOutput::serialize() const
{
    return m_settings.serialize();
}

bool RemoteOutput::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureRemoteOutput* message = MsgConfigureRemoteOutput::create(m_settings, QList<QString>(), true);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgConfigureRemoteOutput* messageToGUI = MsgConfigureRemoteOutput::create(m_settings, QList<QString>(), true);
        m_guiMessageQueue->push(messageToGUI);
    }

    return success;
}

const QString& RemoteOutput::getDeviceDescription() const
{
	return m_deviceDescription;
}

int RemoteOutput::getSampleRate() const
{
	return m_sampleRate;
}

quint64 RemoteOutput::getCenterFrequency() const
{
	return m_centerFrequency;
}

std::time_t RemoteOutput::getStartingTimeStamp() const
{
	return m_startingTimeStamp;
}

bool RemoteOutput::handleMessage(const Message& message)
{

    if (MsgConfigureRemoteOutput::match(message))
    {
        qDebug() << "RemoteOutput::handleMessage:" << message.getIdentifier();
        auto& conf = (const MsgConfigureRemoteOutput&) message;
        applySettings(conf.getSettings(), conf.getSettingsKeys(), conf.getForce());
        return true;
    }
	else if (MsgConfigureRemoteOutputWork::match(message))
	{
		auto& conf = (const MsgConfigureRemoteOutputWork&) message;
		bool working = conf.isWorking();

		if (m_remoteOutputWorker != nullptr)
		{
			if (working) {
                startWorker();
			} else {
                stopWorker();
			}
		}

		return true;
	}
    else if (MsgStartStop::match(message))
    {
        auto& cmd = (const MsgStartStop&) message;
        qDebug() << "RemoteOutput::handleMessage: MsgStartStop: " << (cmd.getStartStop() ? "start" : "stop");

        if (cmd.getStartStop())
        {
            if (m_deviceAPI->initDeviceEngine()) {
                m_deviceAPI->startDeviceEngine();
            }
        }
        else
        {
            m_deviceAPI->stopDeviceEngine();
        }

        if (m_settings.m_useReverseAPI) {
            webapiReverseSendStartStop(cmd.getStartStop());
        }

        return true;
    }
	else if (MsgConfigureRemoteOutputChunkCorrection::match(message))
	{
        auto& conf = (const MsgConfigureRemoteOutputChunkCorrection&) message;

    setWorkerChunkCorrection(conf.getChunkCorrection());

        return true;
	}
    else if (MsgRequestFixedData::match(message))
    {
        QString reportURL;

        reportURL = QString("http://%1:%2/sdrangel")
            .arg(m_settings.m_apiAddress)
            .arg(m_settings.m_apiPort);

        m_networkRequest.setUrl(QUrl(reportURL));
        m_networkManager->get(m_networkRequest);

        return true;
    }
	else
	{
		return false;
	}
}

void RemoteOutput::applySettings(const RemoteOutputSettings& settings, const QList<QString>& settingsKeys, bool force)
{
    RemoteOutputSettings sanitizedSettings = settings;
    sanitizedSettings.m_sampleRate = RemoteOutputSettings::clampSampleRate(sanitizedSettings.m_sampleRate);

    qDebug() << "RemoteOutput::applySettings: force:" << force << sanitizedSettings.getDebugString(settingsKeys, force);

    const bool hadManualSampleRateOverride = m_settings.m_overrideRemoteSampleRate;

    if (force ||
        settingsKeys.contains("apiAddress") ||
        settingsKeys.contains("apiPort") ||
        settingsKeys.contains("deviceIndex") ||
        settingsKeys.contains("channelIndex"))
    {
        m_remoteReportMode = RemoteReportMode::Unknown;
    }

    QMutexLocker mutexLocker(&m_mutex);

    if ((force ||
        settingsKeys.contains("dataAddress") ||
        settingsKeys.contains("dataPort")) && m_remoteOutputWorker)
    {
        setWorkerDataAddress(sanitizedSettings.m_dataAddress, sanitizedSettings.m_dataPort);
    }

    if ((force || settingsKeys.contains("nbFECBlocks")) && m_remoteOutputWorker)
    {
        setWorkerNbBlocksFEC(sanitizedSettings.m_nbFECBlocks);
    }

    if ((force || settingsKeys.contains("nbTxBytes")) && m_remoteOutputWorker)
    {
        stopWorker();
        setWorkerNbTxBytes(sanitizedSettings.m_nbTxBytes);
        startWorker();
    }

    mutexLocker.unlock();

    if (sanitizedSettings.m_useReverseAPI)
    {
        bool fullUpdate = (settingsKeys.contains("useReverseAPI") && sanitizedSettings.m_useReverseAPI) ||
            settingsKeys.contains("reverseAPIAddress") ||
            settingsKeys.contains("reverseAPIPort") ||
            settingsKeys.contains("reverseAPIDeviceIndex");
        webapiReverseSendSettings(settingsKeys, sanitizedSettings, fullUpdate || force);
    }

    if ((force || settingsKeys.contains("sampleRate") || settingsKeys.contains("overrideRemoteSampleRate"))
        && sanitizedSettings.m_overrideRemoteSampleRate
        && (sanitizedSettings.m_sampleRate > 0)
        && (static_cast<int>(sanitizedSettings.m_sampleRate) != m_sampleRate))
    {
        m_sampleRate = sanitizedSettings.m_sampleRate;
        applySampleRate();
    }

    if (force) {
        m_settings = sanitizedSettings;
    } else {
        m_settings.applySettings(settingsKeys, sanitizedSettings);
    }

    if (hadManualSampleRateOverride && !m_settings.m_overrideRemoteSampleRate)
    {
        requestRemoteReport(m_remoteReportMode == RemoteReportMode::RemoteInput ? RemoteReportMode::RemoteInput : RemoteReportMode::Unknown);
    }
}

void RemoteOutput::applyCenterFrequency()
{
    auto *notif = new DSPSignalNotification(m_sampleRate, m_centerFrequency);
    m_deviceAPI->getDeviceEngineInputMessageQueue()->push(notif);
}

void RemoteOutput::applySampleRate()
{
    setWorkerSampleRate(m_sampleRate);

    m_tickMultiplier = 480000 / m_sampleRate;
    m_tickMultiplier = m_tickMultiplier < 1 ? 1 : m_tickMultiplier;
    m_tickMultiplier = m_tickMultiplier > 10 ? 10 : m_tickMultiplier;
    m_greaterTickCount = 0;

    auto *notif = new DSPSignalNotification(m_sampleRate, m_centerFrequency);
    m_deviceAPI->getDeviceEngineInputMessageQueue()->push(notif);
}

void RemoteOutput::requestRemoteReport(RemoteReportMode mode)
{
    QString reportURL;

    if (mode == RemoteReportMode::RemoteInput)
    {
        reportURL = QString("http://%1:%2/sdrangel/deviceset/%3/device/report")
            .arg(m_settings.m_apiAddress)
            .arg(m_settings.m_apiPort)
            .arg(m_settings.m_deviceIndex);
    }
    else
    {
        reportURL = QString("http://%1:%2/sdrangel/deviceset/%3/channel/%4/report")
            .arg(m_settings.m_apiAddress)
            .arg(m_settings.m_apiPort)
            .arg(m_settings.m_deviceIndex)
            .arg(m_settings.m_channelIndex);
    }

    m_networkRequest.setUrl(QUrl(reportURL));
    m_networkManager->get(m_networkRequest);
}

void RemoteOutput::requestRemoteInputSettings()
{
    const auto settingsURL = QString("http://%1:%2/sdrangel/deviceset/%3/device/settings")
        .arg(m_settings.m_apiAddress)
        .arg(m_settings.m_apiPort)
        .arg(m_settings.m_deviceIndex);

    m_networkRequest.setUrl(QUrl(settingsURL));
    m_networkManager->get(m_networkRequest);
}

void RemoteOutput::reportRemoteStatus(const QString& message, bool usesDeviceReport, quint64 centerFrequency, int sampleRate)
{
    if (!m_guiMessageQueue) {
        return;
    }

    auto *msg = MsgReportRemoteStatus::create(message, usesDeviceReport, centerFrequency, sampleRate);
    m_guiMessageQueue->push(msg);
}

int RemoteOutput::webapiRunGet(
        SWGSDRangel::SWGDeviceState& response,
        QString& errorMessage)
{
    (void) errorMessage;
    m_deviceAPI->getDeviceEngineStateStr(*response.getState());
    return 200;
}

int RemoteOutput::webapiRun(
        bool run,
        SWGSDRangel::SWGDeviceState& response,
        QString& errorMessage)
{
    (void) errorMessage;
    m_deviceAPI->getDeviceEngineStateStr(*response.getState());
    MsgStartStop *message = MsgStartStop::create(run);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgStartStop *messagetoGui = MsgStartStop::create(run);
        m_guiMessageQueue->push(messagetoGui);
    }

    return 200;
}

int RemoteOutput::webapiSettingsGet(
                SWGSDRangel::SWGDeviceSettings& response,
                QString& errorMessage)
{
    (void) errorMessage;
    response.setRemoteOutputSettings(new SWGSDRangel::SWGRemoteOutputSettings());
    response.getRemoteOutputSettings()->init();
    webapiFormatDeviceSettings(response, m_settings);
    return 200;
}

int RemoteOutput::webapiSettingsPutPatch(
                bool force,
                const QStringList& deviceSettingsKeys,
                SWGSDRangel::SWGDeviceSettings& response, // query + response
                QString& errorMessage)
{
    (void) errorMessage;
    RemoteOutputSettings settings = m_settings;
    webapiUpdateDeviceSettings(settings, deviceSettingsKeys, response);

    MsgConfigureRemoteOutput *msg = MsgConfigureRemoteOutput::create(settings, deviceSettingsKeys, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureRemoteOutput *msgToGUI = MsgConfigureRemoteOutput::create(settings, deviceSettingsKeys, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatDeviceSettings(response, settings);
    return 200;
}

void RemoteOutput::webapiUpdateDeviceSettings(
        RemoteOutputSettings& settings,
        const QStringList& deviceSettingsKeys,
        SWGSDRangel::SWGDeviceSettings& response)
{
    if (deviceSettingsKeys.contains("title")) {
        settings.m_title = *response.getRemoteOutputSettings()->getTitle();
    }
    if (deviceSettingsKeys.contains("nbFECBlocks")) {
        settings.m_nbFECBlocks = response.getRemoteOutputSettings()->getNbFecBlocks();
    }
    if (deviceSettingsKeys.contains("nbTxBytes")) {
        settings.m_nbTxBytes = response.getRemoteOutputSettings()->getNbTxBytes();
    }
    if (deviceSettingsKeys.contains("apiAddress")) {
        settings.m_apiAddress = *response.getRemoteOutputSettings()->getApiAddress();
    }
    if (deviceSettingsKeys.contains("apiPort")) {
        settings.m_apiPort = (quint16) response.getRemoteOutputSettings()->getApiPort();
    }
    if (deviceSettingsKeys.contains("dataAddress")) {
        settings.m_dataAddress = *response.getRemoteOutputSettings()->getDataAddress();
    }
    if (deviceSettingsKeys.contains("dataPort")) {
        settings.m_dataPort = (quint16) response.getRemoteOutputSettings()->getDataPort();
    }
    if (deviceSettingsKeys.contains("deviceIndex")) {
        settings.m_deviceIndex = response.getRemoteOutputSettings()->getDeviceIndex();
    }
    if (deviceSettingsKeys.contains("channelIndex")) {
        settings.m_channelIndex = response.getRemoteOutputSettings()->getChannelIndex();
    }
    if (deviceSettingsKeys.contains("useReverseAPI")) {
        settings.m_useReverseAPI = response.getRemoteOutputSettings()->getUseReverseApi() != 0;
    }
    if (deviceSettingsKeys.contains("reverseAPIAddress")) {
        settings.m_reverseAPIAddress = *response.getRemoteOutputSettings()->getReverseApiAddress();
    }
    if (deviceSettingsKeys.contains("reverseAPIPort")) {
        settings.m_reverseAPIPort = (quint16) response.getRemoteOutputSettings()->getReverseApiPort();
    }
    if (deviceSettingsKeys.contains("reverseAPIDeviceIndex")) {
        settings.m_reverseAPIDeviceIndex = (uint16_t) response.getRemoteOutputSettings()->getReverseApiDeviceIndex();
    }
}

int RemoteOutput::webapiReportGet(
        SWGSDRangel::SWGDeviceReport& response,
        QString& errorMessage)
{
    (void) errorMessage;
    response.setRemoteOutputReport(new SWGSDRangel::SWGRemoteOutputReport());
    response.getRemoteOutputReport()->init();
    webapiFormatDeviceReport(response);
    return 200;
}

void RemoteOutput::webapiFormatDeviceSettings(SWGSDRangel::SWGDeviceSettings& response, const RemoteOutputSettings& settings)
{
    if (response.getRemoteOutputSettings()->getTitle()) {
        *response.getRemoteOutputSettings()->getTitle() = settings.m_title;
    } else {
        response.getRemoteOutputSettings()->setTitle(new QString(settings.m_title));
    }

    response.getRemoteOutputSettings()->setNbFecBlocks(settings.m_nbFECBlocks);
    response.getRemoteOutputSettings()->setNbTxBytes(settings.m_nbTxBytes);
    response.getRemoteOutputSettings()->setApiAddress(new QString(settings.m_apiAddress));
    response.getRemoteOutputSettings()->setApiPort(settings.m_apiPort);
    response.getRemoteOutputSettings()->setDataAddress(new QString(settings.m_dataAddress));
    response.getRemoteOutputSettings()->setDataPort(settings.m_dataPort);
    response.getRemoteOutputSettings()->setDeviceIndex(settings.m_deviceIndex);
    response.getRemoteOutputSettings()->setChannelIndex(settings.m_channelIndex);
    response.getRemoteOutputSettings()->setUseReverseApi(settings.m_useReverseAPI ? 1 : 0);

    if (response.getRemoteOutputSettings()->getReverseApiAddress()) {
        *response.getRemoteOutputSettings()->getReverseApiAddress() = settings.m_reverseAPIAddress;
    } else {
        response.getRemoteOutputSettings()->setReverseApiAddress(new QString(settings.m_reverseAPIAddress));
    }

    response.getRemoteOutputSettings()->setReverseApiPort(settings.m_reverseAPIPort);
    response.getRemoteOutputSettings()->setReverseApiDeviceIndex(settings.m_reverseAPIDeviceIndex);
}

void RemoteOutput::webapiFormatDeviceReport(SWGSDRangel::SWGDeviceReport& response) const
{
    uint64_t nowus = TimeUtil::nowus();
    response.getRemoteOutputReport()->setTvSec((qint32) (nowus / 1000000U));
    response.getRemoteOutputReport()->setTvUSec(nowus % 1000000U);
    response.getRemoteOutputReport()->setCenterFrequency(m_centerFrequency);
    response.getRemoteOutputReport()->setSampleRate(m_sampleRate);
    response.getRemoteOutputReport()->setQueueSize(m_queueSize);
    response.getRemoteOutputReport()->setQueueLength(m_queueLength);
    response.getRemoteOutputReport()->setSampleCount(m_remoteOutputWorker ? (int) m_remoteOutputWorker->getSamplesCount() : 0);
    response.getRemoteOutputReport()->setCorrectableErrorsCount(m_recoverableCount);
    response.getRemoteOutputReport()->setUncorrectableErrorsCount(m_unrecoverableCount);
}

void RemoteOutput::tick()
{
    if (++m_tickCount == 20) // Every second
    {
        requestRemoteReport(m_remoteReportMode == RemoteReportMode::RemoteInput ? RemoteReportMode::RemoteInput : RemoteReportMode::Unknown);

        m_tickCount = 0;
    }
}

void RemoteOutput::networkManagerFinished(QNetworkReply *reply)
{
    if (reply->error())
    {
        const auto replyPath = reply->url().path();

        if (replyPath.endsWith("/report") && replyPath.contains("/channel/"))
        {
            reply->deleteLater();
            requestRemoteReport(RemoteReportMode::RemoteInput);
            return;
        }

        qInfo("RemoteOutput::networkManagerFinished: error: %s", qPrintable(reply->errorString()));
    }
    else
    {
        QString answer = reply->readAll();

        try
        {
            QByteArray jsonBytes(answer.toStdString().c_str());
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &error);

            if (error.error == QJsonParseError::NoError)
            {
                analyzeApiReply(doc.object(), answer);
            }
            else
            {
                QString errorMsg = QString("Reply JSON error: ") + error.errorString() + QString(" at offset ") + QString::number(error.offset);
                qInfo().noquote() << "RemoteOutput::networkManagerFinished" << errorMsg;
            }
        }
        catch (const std::exception& ex)
        {
            QString errorMsg = QString("Error parsing request: ") + ex.what();
            qInfo().noquote() << "RemoteOutput::networkManagerFinished" << errorMsg;
        }
    }

    reply->deleteLater();
}

void RemoteOutput::analyzeApiReply(const QJsonObject& jsonObject, const QString& answer)
{
    if (jsonObject.contains("RemoteSourceReport"))
    {
        if (m_remoteReportMode != RemoteReportMode::RemoteSource)
        {
            m_remoteReportMode = RemoteReportMode::RemoteSource;
            reportRemoteStatus("RemoteSource receiver detected.", false, 0, 0);
        }

        MsgReportRemoteData::RemoteData msgRemoteData;
        QJsonObject report = jsonObject["RemoteSourceReport"].toObject();
        uint64_t centerFrequency = report["deviceCenterFreq"].toInt() + report["centerFreq"].toInt();

        if (centerFrequency != m_centerFrequency)
        {
            m_centerFrequency = centerFrequency;
            applyCenterFrequency();
        }

        int remoteRate = report["sampleRate"].toInt();

        if (!m_settings.m_overrideRemoteSampleRate && (remoteRate != m_sampleRate))
        {
            m_sampleRate = remoteRate;
            applySampleRate();
        }

        msgRemoteData.m_centerFrequency = m_centerFrequency;
        msgRemoteData.m_sampleRate = m_sampleRate;

        m_queueSize = report["queueSize"].toInt();
        m_queueSize = m_queueSize == 0 ? 20 : m_queueSize;
        msgRemoteData.m_queueSize = m_queueSize;
        m_queueLength = report["queueLength"].toInt();
        msgRemoteData.m_queueLength = m_queueLength;
        uint64_t remoteTimestampUs = report["tvSec"].toInt()*1000000ULL + report["tvUSec"].toInt();
        msgRemoteData.m_timestampUs = remoteTimestampUs;
        int intRemoteSampleCount = report["samplesCount"].toInt();
        uint32_t remoteSampleCount = intRemoteSampleCount < 0 ? 0 : intRemoteSampleCount;
        msgRemoteData.m_sampleCount = remoteSampleCount;
        m_unrecoverableCount = report["uncorrectableErrorsCount"].toInt();
        msgRemoteData.m_unrecoverableCount = m_unrecoverableCount;
        m_recoverableCount = report["correctableErrorsCount"].toInt();
        msgRemoteData.m_recoverableCount = m_recoverableCount;

        if (m_guiMessageQueue)
        {
            MsgReportRemoteData *msg = MsgReportRemoteData::create(msgRemoteData);
            m_guiMessageQueue->push(msg);
        }

        if (!m_remoteOutputWorker) {
            return;
        }

        if (++m_greaterTickCount == m_tickMultiplier)
        {
            queueLengthCompensation(m_sampleRate, m_queueLength, m_queueSize);
            m_greaterTickCount = 0;
        }
    }
    else if (jsonObject.contains("RemoteInputReport"))
    {
        if (m_remoteReportMode != RemoteReportMode::RemoteInput)
        {
            m_remoteReportMode = RemoteReportMode::RemoteInput;
        }

        const auto report = jsonObject["RemoteInputReport"].toObject();
        const auto centerFrequency = static_cast<quint64>(report["centerFrequency"].toDouble());
        const auto sampleRate = report["sampleRate"].toInt();

        if (centerFrequency != m_centerFrequency)
        {
            m_centerFrequency = centerFrequency;
            applyCenterFrequency();
        }

        if (!m_settings.m_overrideRemoteSampleRate && (sampleRate > 0) && (sampleRate != m_sampleRate))
        {
            m_sampleRate = sampleRate;
            applySampleRate();
        }

        reportRemoteStatus(
            "RemoteInput receiver detected. Channel index is ignored.",
            true,
            m_centerFrequency,
            m_sampleRate);

        requestRemoteInputSettings();
    }
    else if (jsonObject.contains("remoteInputSettings"))
    {
        const auto settingsObject = jsonObject["remoteInputSettings"].toObject();
        const auto dataAddress = settingsObject["dataAddress"].toString();
        const auto multicastAddress = settingsObject["multicastAddress"].toString();
        const auto multicastJoinValue = settingsObject.value("multicastJoin");
        const bool multicastJoin = multicastJoinValue.isBool() ?
            multicastJoinValue.toBool() :
            (multicastJoinValue.toInt() != 0);
        const auto targetAddress = (multicastJoin && !multicastAddress.isEmpty()) ? multicastAddress : dataAddress;
        const auto dataPort = static_cast<quint16>(settingsObject["dataPort"].toInt());

        QList<QString> settingsKeys;
        RemoteOutputSettings settings = m_settings;

        if (!targetAddress.isEmpty() && (settings.m_dataAddress != targetAddress))
        {
            settings.m_dataAddress = targetAddress;
            settingsKeys.append("dataAddress");
        }

        if ((dataPort != 0) && (settings.m_dataPort != dataPort))
        {
            settings.m_dataPort = dataPort;
            settingsKeys.append("dataPort");
        }

        if (!settingsKeys.isEmpty())
        {
            applySettings(settings, settingsKeys, false);

            if (m_guiMessageQueue)
            {
                auto *msg = MsgConfigureRemoteOutput::create(settings, settingsKeys, false);
                m_guiMessageQueue->push(msg);
            }
        }

        reportRemoteStatus(
            QString("RemoteInput receiver detected. Streaming to %1:%2.")
                .arg(settings.m_dataAddress)
                .arg(settings.m_dataPort),
            true,
            m_centerFrequency,
            m_sampleRate);
    }
    else if (jsonObject.contains("remoteOutputSettings"))
    {
        qDebug("RemoteOutput::analyzeApiReply: reply:\n%s", answer.toStdString().c_str());
    }
    else if (jsonObject.contains("version"))
    {
        MsgReportRemoteFixedData::RemoteData msgRemoteFixedData;
        msgRemoteFixedData.m_version = jsonObject["version"].toString();

        if (jsonObject.contains("qtVersion")) {
            msgRemoteFixedData.m_qtVersion = jsonObject["qtVersion"].toString();
        }

        if (jsonObject.contains("architecture")) {
            msgRemoteFixedData.m_architecture = jsonObject["architecture"].toString();
        }

        if (jsonObject.contains("os")) {
            msgRemoteFixedData.m_os = jsonObject["os"].toString();
        }

        if (jsonObject.contains("dspRxBits") && jsonObject.contains("dspTxBits"))
        {
            msgRemoteFixedData.m_rxBits = jsonObject["dspRxBits"].toInt();
            msgRemoteFixedData.m_txBits = jsonObject["dspTxBits"].toInt();
        }

        if (m_guiMessageQueue)
        {
            MsgReportRemoteFixedData *msg = MsgReportRemoteFixedData::create(msgRemoteFixedData);
            m_guiMessageQueue->push(msg);
        }
    }
}

void RemoteOutput::queueLengthCompensation(
    int nominalSR,
    int queueLength,
    int queueSize
)
{
    int deltaQueueBlocks = (queueSize/2) - queueLength;
    int blockMultiplier = nominalSR / 4000;
    blockMultiplier = blockMultiplier < 12 ? 12 : blockMultiplier;
    int corr = deltaQueueBlocks * blockMultiplier;
    qDebug("RemoteOutput::queueLengthCompensation: deltaQueueBlocks: %d corr: %d", deltaQueueBlocks, corr);
    MsgConfigureRemoteOutputChunkCorrection* message = MsgConfigureRemoteOutputChunkCorrection::create(corr);
    getInputMessageQueue()->push(message);
}

void RemoteOutput::webapiReverseSendSettings(const QList<QString>& deviceSettingsKeys, const RemoteOutputSettings& settings, bool force)
{
    auto *swgDeviceSettings = new SWGSDRangel::SWGDeviceSettings();
    swgDeviceSettings->setDirection(1); // single Tx
    swgDeviceSettings->setOriginatorIndex(m_deviceAPI->getDeviceSetIndex());
    swgDeviceSettings->setDeviceHwType(new QString("RemoteOutput"));
    swgDeviceSettings->setRemoteOutputSettings(new SWGSDRangel::SWGRemoteOutputSettings());
    SWGSDRangel::SWGRemoteOutputSettings *swgRemoteOutputSettings = swgDeviceSettings->getRemoteOutputSettings();

    // transfer data that has been modified. When force is on transfer all data except reverse API data

    if (deviceSettingsKeys.contains("title") || force) {
        swgRemoteOutputSettings->setTitle(new QString(settings.m_title));
    }
    if (deviceSettingsKeys.contains("nbFECBlocks") || force) {
        swgRemoteOutputSettings->setNbFecBlocks(settings.m_nbFECBlocks);
    }
    if (deviceSettingsKeys.contains("nbTxBytes") || force) {
        swgRemoteOutputSettings->setNbTxBytes(settings.m_nbTxBytes);
    }
    if (deviceSettingsKeys.contains("apiAddress") || force) {
        swgRemoteOutputSettings->setApiAddress(new QString(settings.m_apiAddress));
    }
    if (deviceSettingsKeys.contains("apiPort") || force) {
        swgRemoteOutputSettings->setApiPort(settings.m_apiPort);
    }
    if (deviceSettingsKeys.contains("dataAddress") || force) {
        swgRemoteOutputSettings->setDataAddress(new QString(settings.m_dataAddress));
    }
    if (deviceSettingsKeys.contains("dataPort") || force) {
        swgRemoteOutputSettings->setDataPort(settings.m_dataPort);
    }
    if (deviceSettingsKeys.contains("deviceIndex") || force) {
        swgRemoteOutputSettings->setDeviceIndex(settings.m_deviceIndex);
    }
    if (deviceSettingsKeys.contains("channelIndex") || force) {
        swgRemoteOutputSettings->setChannelIndex(settings.m_channelIndex);
    }

    QString deviceSettingsURL = QString("http://%1:%2/sdrangel/deviceset/%3/device/settings")
            .arg(settings.m_reverseAPIAddress)
            .arg(settings.m_reverseAPIPort)
            .arg(settings.m_reverseAPIDeviceIndex);
    m_networkRequest.setUrl(QUrl(deviceSettingsURL));
    m_networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *buffer = new QBuffer();
    buffer->open(QBuffer::ReadWrite);
    buffer->write(swgDeviceSettings->asJson().toUtf8());
    buffer->seek(0);

    // Always use PATCH to avoid passing reverse API settings
    QNetworkReply *reply = m_networkManager->sendCustomRequest(m_networkRequest, "PATCH", buffer);
    buffer->setParent(reply);

    delete swgDeviceSettings;
}

void RemoteOutput::webapiReverseSendStartStop(bool start)
{
    auto *swgDeviceSettings = new SWGSDRangel::SWGDeviceSettings();
    swgDeviceSettings->setDirection(1); // single Tx
    swgDeviceSettings->setOriginatorIndex(m_deviceAPI->getDeviceSetIndex());
    swgDeviceSettings->setDeviceHwType(new QString("RemoteOutput"));

    QString deviceSettingsURL = QString("http://%1:%2/sdrangel/deviceset/%3/device/run")
            .arg(m_settings.m_reverseAPIAddress)
            .arg(m_settings.m_reverseAPIPort)
            .arg(m_settings.m_reverseAPIDeviceIndex);
    m_networkRequest.setUrl(QUrl(deviceSettingsURL));
    m_networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    auto *buffer = new QBuffer();
    buffer->open(QBuffer::ReadWrite);
    buffer->write(swgDeviceSettings->asJson().toUtf8());
    buffer->seek(0);
    QNetworkReply *reply;

    if (start) {
        reply = m_networkManager->sendCustomRequest(m_networkRequest, "POST", buffer);
    } else {
        reply = m_networkManager->sendCustomRequest(m_networkRequest, "DELETE", buffer);
    }

    buffer->setParent(reply);
    delete swgDeviceSettings;
}
