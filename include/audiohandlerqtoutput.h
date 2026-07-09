#ifndef AUDIOHANDLERQTOUTPUT_H
#define AUDIOHANDLERQTOUTPUT_H
#include "audiohandlerbase.h"

#ifdef Q_OS_ANDROID
#include <QMutex>
#include <QMutexLocker>
#include <QElapsedTimer>

// Pull-mode source for QAudioSink on Android. Qt 6.8's Android backend is
// unusable in push mode (bytesFree() stays 0 and write() rejects data while
// the device still underruns), so the sink reads from this ring buffer
// instead. Gaps are zero-filled: the device never underruns, missing
// packets just play as silence.
class QtOutputRingDevice : public QIODevice
{
public:
    explicit QtOutputRingDevice(QObject* parent = nullptr) : QIODevice(parent) {}
    void setCapacity(qint64 bytes) { QMutexLocker l(&m); cap = bytes; }
    bool isSequential() const override { return true; }
    void append(const QByteArray& d)
    {
        QMutexLocker l(&m);
        buf.append(d);
        if (cap > 0 && buf.size() > cap)
            buf.remove(0, buf.size() - cap); // overflow: drop oldest audio
    }
    qint64 queuedBytes() const { QMutexLocker l(&m); return buf.size(); }

    // The backend checks bytesAvailable() before pulling; the default
    // implementation reports 0 for data held outside QIODevice's own
    // buffer, which stops the sink from ever reading.
    qint64 bytesAvailable() const override
    {
        QMutexLocker l(&m);
        return buf.size() + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char* out, qint64 maxlen) override
    {
        QMutexLocker l(&m);
        const qint64 n = qMin(maxlen, (qint64)buf.size());
        if (n > 0) {
            memcpy(out, buf.constData(), n);
            buf.remove(0, n);
        }
        if (n < maxlen) {
            memset(out + n, 0, maxlen - n);
        }
        return maxlen;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

private:
    mutable QMutex m;
    QByteArray buf;
    qint64 cap {0};
};
#endif

class audioHandlerQtOutput : public audioHandlerBase
{
    Q_OBJECT

public:
    explicit audioHandlerQtOutput(QObject* parent = nullptr) : audioHandlerBase(parent) {}
    ~audioHandlerQtOutput() override { dispose(); } // ensure close on destruction
    QString role() const override { return QStringLiteral("Output"); }

public slots:
    void incomingAudio(audioPacket packet);

protected:
    bool openDevice() noexcept override;
    void closeDevice() noexcept override;
    virtual QAudioFormat getNativeFormat() override;
    virtual bool isFormatSupported(QAudioFormat f) override;

private:
    void writeToOutputDevice(QByteArray data, quint32 seq, float amplitudePeak, float amplitudeRms);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    QAudioOutput*    audioOutput {nullptr};
#else
    QAudioSink*      audioOutput {nullptr};
#endif

    QIODevice*       audioDevice {nullptr};
#ifdef Q_OS_ANDROID
    QtOutputRingDevice* ringDevice {nullptr};
#endif

private slots:
    void onConverted(audioPacket pkt);

};

#endif // AUDIOHANDLERQTOUTPUT_H
