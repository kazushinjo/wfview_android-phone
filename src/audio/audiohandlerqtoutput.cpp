// =============================
// audiohandleroutput.cpp
// =============================
#include "audiohandlerqtoutput.h"
#ifdef WFVIEW_PHONE
#include "phonerecorder.h"
#endif
#include <QThread>
#include <QElapsedTimer>

bool audioHandlerQtOutput::openDevice() noexcept
{
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    audioOutput = new QAudioOutput(deviceInfo, nativeFormat, this);
    connect(audioOutput, SIGNAL(stateChanged(QAudio::State)), this, SLOT(stateChanged(QAudio::State)));
#else
    audioOutput = new QAudioSink(deviceInfo, nativeFormat, this);
    connect(audioOutput, &QAudioSink::stateChanged, this, &audioHandlerQtOutput::stateChanged);
#endif

    emit setupConverter(radioFormat,  codec,
                        nativeFormat, codecType::LPCM,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));

#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
    audioOutput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 100));
#else
    audioOutput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 1000));
#endif

#ifdef Q_OS_ANDROID
    // Pull mode: Qt 6.8's Android backend is unusable in push mode (see
    // QtOutputRingDevice). The sink drains the ring buffer via its own
    // callback; converted packets are appended in writeToOutputDevice().
    ringDevice = new QtOutputRingDevice(this);
    ringDevice->setCapacity(nativeFormat.bytesForDuration(setupData.latency * 1000));
    ringDevice->open(QIODevice::ReadOnly);
    audioOutput->start(ringDevice);
    audioDevice = ringDevice;
    if (audioOutput->state() == QAudio::StoppedState) return false;
    qInfo(logAudio()) << "Qt audio output (pull mode): ring capacity(ms):" << setupData.latency
                      << "format:" << nativeFormat;
#else
    audioDevice = audioOutput->start();
    if (!audioDevice) return false;

    qInfo(logAudio()) << "Qt audio output: requested buffer(ms):" << setupData.latency
                      << "actual buffer bytes:" << audioOutput->bufferSize()
                      << "(" << (int)(nativeFormat.durationForBytes(audioOutput->bufferSize())/1000) << "ms )"
                      << "format:" << nativeFormat;

    // Pre-fill half the buffer with silence so ALSA has data to pull
    // before the first real audio packet arrives from the network.
    {
        const int prefillBytes = audioOutput->bufferSize() / 2;
        QByteArray silence(prefillBytes, '\0');
        audioDevice->write(silence.constData(), silence.size());
    }

    connect(audioOutput, SIGNAL(destroyed()), audioDevice, SLOT(deleteLater()), Qt::UniqueConnection);
#endif
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    qInfo(logAudio()) << "Connected to Qt audio output device" << deviceInfo.deviceName();
#else
    qInfo(logAudio()) << "Connected to Qt audio output device" << deviceInfo.description();
#endif
    return true;
}

void audioHandlerQtOutput::closeDevice() noexcept
{
    if (audioOutput) {
        if (audioOutput->state() != QAudio::StoppedState) audioOutput->stop();
        audioOutput->deleteLater();
        audioOutput = nullptr;
    }
    audioDevice = nullptr;
#ifdef Q_OS_ANDROID
    ringDevice = nullptr; // owned by this, deleted via QObject parent
#endif
}

void audioHandlerQtOutput::incomingAudio(audioPacket packet)
{
    if (!audioDevice || packet.data.isEmpty()) return;
    packet.volume = volume;
    emit sendToConverter(packet);
}

void audioHandlerQtOutput::onConverted(audioPacket pkt)
{
    if (!audioOutput || !audioDevice || pkt.data.isEmpty()) return;
    const int nowToPktMs = pkt.time.msecsTo(QTime::currentTime());
    if (nowToPktMs > setupData.latency * 1.5) {
        return; // late frame ( more than latency * 1.5), drop
    }
    writeToOutputDevice(pkt.data, pkt.seq, pkt.amplitudePeak, pkt.amplitudeRMS);
}

void audioHandlerQtOutput::writeToOutputDevice(QByteArray data, quint32 seq, float ampPeak, float ampRms)
{
    Q_UNUSED(seq);
    if (!audioOutput || !audioDevice) return;

#ifndef Q_OS_ANDROID
    // Recover from underrun: re-prime the buffer with silence so the device
    // has a cushion before real audio resumes, preventing click cascades.
    if (isUnderrun.load(std::memory_order_relaxed)) {
        const int prefillBytes = audioOutput->bufferSize() / 2;
        const int freeBytes    = static_cast<int>(audioOutput->bytesFree());
        const int silenceBytes = qMin(prefillBytes, freeBytes);
        if (silenceBytes > 0) {
            QByteArray silence(silenceBytes, '\0');
            audioDevice->write(silence.constData(), silence.size());
        }
        isUnderrun.store(false, std::memory_order_relaxed);
        if (underTimer && underTimer->isActive()) underTimer->stop();
    }
#endif

#ifdef Q_OS_ANDROID
    qint64 buffered = ringDevice ? ringDevice->queuedBytes() : 0;
#else
    qint64 buffered = audioOutput->bufferSize() - audioOutput->bytesFree();
#endif
    int devLatencyMs = static_cast<int>(nativeFormat.durationForBytes(buffered) / 1000);
    int pipelineMs   = lastReceived.isValid() ? static_cast<int>(lastReceived.elapsed()) : 0;
    int newLatency   = pipelineMs + devLatencyMs;
    int prev = currentLatency.load(std::memory_order_relaxed);
    currentLatency.store(static_cast<int>(prev * 0.8 + newLatency * 0.2), std::memory_order_relaxed);

#ifdef Q_OS_ANDROID
    ringDevice->append(data);
    qint64 toWrite = 0;
#else
    qint64 toWrite = data.size();
    const char* p  = data.constData();
    while (toWrite > 0) {
        qint64 written = audioDevice->write(p, toWrite);
        if (written <= 0) break;
        p       += written;
        toWrite -= written;
    }
#endif

#ifdef WFVIEW_PHONE
    // Tap the RX PCM for the optional on-device recorder (no-op when idle).
    phoneRecorder::getInstance()->feedRx(data.constData(), static_cast<int>(data.size()),
        nativeFormat.sampleRate(), nativeFormat.channelCount(),
        nativeFormat.bytesPerSample(),
        nativeFormat.sampleFormat() == QAudioFormat::Float);
#endif

    lastReceived.restart();
    amplitude.store(ampPeak, std::memory_order_relaxed);
    emit haveLevels(amplitudePeak(), static_cast<quint16>(ampRms * 255.0f), setupData.latency, currentLatency.load(), isUnderrun.load(), isOverrun.load());
}

QAudioFormat audioHandlerQtOutput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool audioHandlerQtOutput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
