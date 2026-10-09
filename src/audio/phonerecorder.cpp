#include "phonerecorder.h"

#include <QStandardPaths>
#include <QDir>
#include <QDateTime>
#include <QDataStream>
#include <QtEndian>
#include <cstring>

phoneRecorder* phoneRecorder::getInstance()
{
    static phoneRecorder instance;
    return &instance;
}

bool phoneRecorder::isRecording()
{
    QMutexLocker locker(&mutex);
    return armed || active;
}

bool phoneRecorder::toggle(QString& pathOut)
{
    QMutexLocker locker(&mutex);

    if (armed || active) {
        // Stop.
        if (active)
            finalize();
        armed = false;
        active = false;
        rxQueue.clear();
        txQueue.clear();
        pathOut = path;
        return false;
    }

    // Start: choose a file now, capture the sample rate from whichever side
    // (RX or TX) delivers its first frame.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QDir().mkpath(dir);
    // Filename: QSO + local (Japan) date-hour-minsec, e.g. QSO20260704-22-1249.wav.
    path = dir + "/QSO"
         + QDateTime::currentDateTime().toString("yyyyMMdd-HH-mmss") + ".wav";
    dataBytes = 0;
    fmtRate = 0;
    rxQueue.clear();
    txQueue.clear();
    armed = true;
    active = false;
    pathOut = path;
    return true;
}

void phoneRecorder::feedRx(const char* data, int len, int sampleRate, int channels,
                         int bytesPerSample, bool isFloat)
{
    feed(false, data, len, sampleRate, channels, bytesPerSample, isFloat);
}

void phoneRecorder::feedTx(const char* data, int len, int sampleRate, int channels,
                         int bytesPerSample, bool isFloat)
{
    feed(true, data, len, sampleRate, channels, bytesPerSample, isFloat);
}

void phoneRecorder::feed(bool isTx, const char* data, int len, int sampleRate,
                        int channels, int bytesPerSample, bool isFloat)
{
    if (len <= 0 || !data || sampleRate <= 0 || channels <= 0 || bytesPerSample <= 0) return;

    QMutexLocker locker(&mutex);
    if (!armed && !active) return;

    if (armed && !active) {
        // First frame from either side after start: lock in the rate and
        // open the file (fixed stereo 16-bit output format regardless of the
        // source formats, which are downmixed/converted per-side below).
        fmtRate = sampleRate;
        file.setFileName(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            armed = false;
            return;
        }
        writeHeader();
        dataBytes = 0;
        active = true;
        armed = false;
    }

    if (!active) return;

    QVector<qint16>& q = isTx ? txQueue : rxQueue;
    toMonoSamples(data, len, channels, bytesPerSample, isFloat, q);
    if (q.size() > kMaxQueuedSamples)
        q.remove(0, q.size() - kMaxQueuedSamples); // drop oldest, keep memory bounded

    drainInterleaved();
}

// Downmix an incoming PCM block (any of the formats CoreAudio/Qt hand us) to
// mono 16-bit samples and append them to the given queue.
void phoneRecorder::toMonoSamples(const char* data, int len, int channels,
                                 int bytesPerSample, bool isFloat, QVector<qint16>& out)
{
    const int frameBytes = channels * bytesPerSample;
    if (frameBytes <= 0) return;
    const int frames = len / frameBytes;
    out.reserve(out.size() + frames);

    for (int f = 0; f < frames; ++f) {
        const char* frame = data + f * frameBytes;
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c) {
            const char* s = frame + c * bytesPerSample;
            float v = 0.0f;
            if (isFloat && bytesPerSample == 4) {
                float fv;
                memcpy(&fv, s, 4);
                v = fv * 32767.0f;
            } else if (!isFloat && bytesPerSample == 2) {
                qint16 iv;
                memcpy(&iv, s, 2);
                v = static_cast<float>(iv);
            } else if (!isFloat && bytesPerSample == 1) {
                // 8-bit unsigned PCM, centred at 128.
                quint8 uv = static_cast<quint8>(*s);
                v = (static_cast<float>(uv) - 128.0f) * 256.0f;
            } else if (!isFloat && bytesPerSample == 4) {
                qint32 iv;
                memcpy(&iv, s, 4);
                v = static_cast<float>(iv) / 65536.0f;
            } else {
                continue; // unsupported format: contributes silence
            }
            sum += v;
        }
        float avg = sum / static_cast<float>(channels);
        if (avg > 32767.0f) avg = 32767.0f;
        if (avg < -32768.0f) avg = -32768.0f;
        out.append(static_cast<qint16>(avg));
    }
}

// Write as many L(RX)/R(TX) sample pairs as both sides currently have queued.
void phoneRecorder::drainInterleaved()
{
    const int n = qMin(rxQueue.size(), txQueue.size());
    if (n <= 0) return;

    QByteArray out;
    out.resize(n * 4); // 2 channels * 2 bytes
    char* p = out.data();
    for (int i = 0; i < n; ++i) {
        qToLittleEndian<qint16>(rxQueue.at(i), p); p += 2;
        qToLittleEndian<qint16>(txQueue.at(i), p); p += 2;
    }
    rxQueue.remove(0, n);
    txQueue.remove(0, n);

    qint64 w = file.write(out.constData(), out.size());
    if (w > 0) dataBytes += static_cast<quint32>(w);
}

// Write a 44-byte WAV header for 16-bit stereo PCM; sizes patched in finalize().
void phoneRecorder::writeHeader()
{
    const quint16 fmtCode    = 1; // PCM
    const quint16 channels   = 2; // L=RX, R=TX
    const quint32 rate       = static_cast<quint32>(fmtRate);
    const quint16 bits       = 16;
    const quint16 blockAlign = static_cast<quint16>(channels * (bits / 8));
    const quint32 byteRate   = rate * blockAlign;

    QByteArray h;
    auto u16 = [&](quint16 v){ char b[2]; qToLittleEndian<quint16>(v, b); h.append(b, 2); };
    auto u32 = [&](quint32 v){ char b[4]; qToLittleEndian<quint32>(v, b); h.append(b, 4); };
    h.append("RIFF"); u32(0);          // RIFF chunk size (patched on stop)
    h.append("WAVE");
    h.append("fmt "); u32(16);
    u16(fmtCode); u16(channels); u32(rate); u32(byteRate); u16(blockAlign); u16(bits);
    h.append("data"); u32(0);          // data chunk size (patched on stop)
    file.write(h.constData(), h.size());
}

void phoneRecorder::finalize()
{
    if (!file.isOpen()) return;
    const quint32 riffSize = 36 + dataBytes;

    auto patch = [&](qint64 pos, quint32 val) {
        file.seek(pos);
        char b[4];
        qToLittleEndian<quint32>(val, b);
        file.write(b, 4);
    };
    patch(4, riffSize);
    patch(40, dataBytes);
    file.close();
}
