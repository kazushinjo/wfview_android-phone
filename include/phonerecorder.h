#ifndef PHONERECORDER_H
#define PHONERECORDER_H

// Records RX (speaker) and TX (microphone) audio simultaneously into one
// stereo WAV: left channel = RX, right channel = TX. Both audio paths run
// continuously once connected (the network audio streams both directions
// regardless of PTT state), so samples from each side are queued and
// interleaved as pairs become available. Process-wide singleton so the
// RX/TX audio threads can feed it PCM directly, with the UI thread only
// starting/stopping it (no cross-object signal plumbing). Writes into the
// app's Documents directory.

#include <QFile>
#include <QMutex>
#include <QString>
#include <QVector>

class phoneRecorder
{
public:
    static phoneRecorder* getInstance();

    // Toggle recording. Returns true if recording is now active (and sets
    // pathOut to the file being written), false if it was stopped.
    bool toggle(QString& pathOut);

    bool isRecording();

    // Called from the RX (speaker) / TX (microphone) audio threads with the
    // PCM currently flowing through that side. The sample rate is locked in
    // from whichever side delivers its first frame after a start request;
    // each side is downmixed to mono and the two are interleaved as stereo
    // (L=RX, R=TX).
    void feedRx(const char* data, int len, int sampleRate, int channels,
                int bytesPerSample, bool isFloat);
    void feedTx(const char* data, int len, int sampleRate, int channels,
                int bytesPerSample, bool isFloat);

private:
    phoneRecorder() = default;
    void feed(bool isTx, const char* data, int len, int sampleRate,
              int channels, int bytesPerSample, bool isFloat);
    void toMonoSamples(const char* data, int len, int channels,
                        int bytesPerSample, bool isFloat, QVector<qint16>& out);
    void drainInterleaved(); // write as many L(RX)/R(TX) pairs as available
    void writeHeader();      // placeholder sizes, patched on stop
    void finalize();         // patch RIFF/data sizes and close

    QMutex mutex;
    QFile file;
    bool armed = false;   // start requested; waiting for first frame's rate
    bool active = false;  // header written, appending audio
    QString path;
    quint32 dataBytes = 0;
    int fmtRate = 0;

    // Per-side mono sample queues awaiting interleaving. Capped so a side
    // that stalls (e.g. a brief audio glitch on one path) can't grow memory
    // unbounded; oldest samples are dropped instead, which just skips that
    // side ahead a little.
    QVector<qint16> rxQueue, txQueue;
    static constexpr int kMaxQueuedSamples = 48000 * 5; // ~5s safety cap
};

#endif // PHONERECORDER_H
