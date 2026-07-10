#include "audiohandlerqtinput.h"

bool audioHandlerQtInput::openDevice() noexcept
{
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    audioInput = new QAudioInput(deviceInfo, nativeFormat, this);
    connect(audioInput, SIGNAL(stateChanged(QAudio::State)), this, SLOT(stateChanged(QAudio::State)));
#else
    audioInput = new QAudioSource(deviceInfo, nativeFormat, this);
    connect(audioInput, &QAudioSource::stateChanged, this, &audioHandlerQtInput::stateChanged);
#endif

    emit setupConverter(nativeFormat, codecType::LPCM,
                        radioFormat,  codec,
                        7, setupData.resampleQuality);

    connect(this, &audioHandlerBase::sendToConverter, converter, &audioConverter::convert);
    connect(converter, SIGNAL(converted(audioPacket)), this, SLOT(onConverted(audioPacket)));

#if (defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6,0,0)))
    audioInput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 100));
#else
    audioInput->setBufferSize(nativeFormat.bytesForDuration(setupData.latency * 1000));
#endif

    audioDevice = audioInput->start();
    if (!audioDevice) return false;

    connect(audioInput, SIGNAL(destroyed()), audioDevice, SLOT(deleteLater()), Qt::UniqueConnection);
    connect(audioDevice, SIGNAL(readyRead()), this, SLOT(onReadyRead()), Qt::UniqueConnection);
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    qInfo(logAudio()) << "Connected to Qt audio input device" << deviceInfo.deviceName();
#else
    qInfo(logAudio()) << "Connected to Qt audio input device" << deviceInfo.description();
#endif
    return true;
}

void audioHandlerQtInput::closeDevice() noexcept
{
    if (audioInput) {
        if (audioInput->state() != QAudio::StoppedState) audioInput->stop();
        audioInput->deleteLater();
        audioInput = nullptr;
    }
    audioDevice = nullptr;
}

void audioHandlerQtInput::onReadyRead()
{
    if (!audioDevice) return;
    tempBuf.data.append(audioDevice->readAll());

    const int bytesPerBlock = nativeFormat.bytesForDuration(setupData.blockSize * 1000);
    while (tempBuf.data.size() >= bytesPerBlock) {
        audioPacket pkt;
        pkt.time   = QTime::currentTime();
        pkt.sent   = 0;
        pkt.volume = volume;
        memcpy(&pkt.guid, setupData.guid, GUIDLEN);
        pkt.data   = tempBuf.data.left(bytesPerBlock);
        tempBuf.data.remove(0, bytesPerBlock);
#ifdef Q_OS_ANDROID
        // Android's raw MIC source is far quieter than desktop inputs
        // (peaks around 0.06 full scale even for close speech), leaving the
        // transmit modulation nearly inaudible. Boost with clipping guard.
        {
            qint16 *s = reinterpret_cast<qint16*>(pkt.data.data());
            const int n = pkt.data.size() / (int)sizeof(qint16);
            for (int i = 0; i < n; ++i) {
                const int v = int(s[i]) * 6;
                s[i] = qint16(qBound(-32768, v, 32767));
            }
        }
#endif
        emit sendToConverter(pkt);
    }
}

void audioHandlerQtInput::onConverted(audioPacket audio)
{
    if (audio.data.isEmpty()) return;

#ifdef Q_OS_ANDROID
    // Diagnostics: confirm mic capture is flowing towards the radio.
    static QElapsedTimer txLogTimer;
    static qint64 txBytes = 0;
    txBytes += audio.data.size();
    if (!txLogTimer.isValid()) txLogTimer.start();
    if (txLogTimer.elapsed() > 1000) {
        qInfo(logAudio()) << "TX mic ->" << txBytes << "B/s peak:" << audio.amplitudePeak;
        txBytes = 0;
        txLogTimer.restart();
    }
#endif

    emit haveAudioData(audio);

    if (lastReceived.isValid() && lastReceived.elapsed() > 100) {
        qDebug(logAudio()) << role() << "Time since last audio packet" << lastReceived.elapsed()
        << "Expected around" << setupData.blockSize;
    }
    if (!lastReceived.isValid()) lastReceived.start(); else lastReceived.restart();

    amplitude.store(audio.amplitudePeak, std::memory_order_relaxed);
    emit haveLevels(amplitudePeak(), audio.amplitudeRMS, setupData.latency,
                    static_cast<quint16>(latencyMs()), isUnderrun.load(), isOverrun.load());
}


QAudioFormat audioHandlerQtInput::getNativeFormat()
{
    return setupData.port.preferredFormat();
}

bool audioHandlerQtInput::isFormatSupported(QAudioFormat f)
{
    return setupData.port.isFormatSupported(f);
}
