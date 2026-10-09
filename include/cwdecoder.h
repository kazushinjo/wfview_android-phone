#ifndef CWDECODER_H
#define CWDECODER_H

// CwDecoder — decodes received Morse (CW) audio into text.
//
// process() is called from the audio converter thread with the raw RX audio
// (before noise reduction).  The decoder:
//   1. decimates to ~8 kHz and runs a small Goertzel filter bank (300–1200 Hz)
//      to find the strongest tone (the CW pitch);
//   2. follows the tone's envelope with adaptive signal/noise levels to decide
//      key-down / key-up every ~8 ms;
//   3. sorts marks into dots and dashes by splitting the recent mark lengths
//      into two groups (speed is tracked automatically) and maps the elements
//      to letters, digits and punctuation (international Morse only).
// Decoded text and the current speed/pitch are emitted as queued signals.

#include <QObject>
#include <QString>
#include <atomic>
#include <deque>
#include <vector>

class CwDecoder : public QObject
{
    Q_OBJECT

public:
    explicit CwDecoder(QObject *parent = nullptr);

    // Any thread.  Disabled decoders ignore process() calls; enabling resets
    // the decoder state.
    void setEnabled(bool en);
    bool isEnabled() const { return m_enabled.load(std::memory_order_relaxed); }

    // Converter thread.  samples: interleaved if channels == 2.
    void process(const float *samples, int count, float sampleRate, int channels);

signals:
    void decodedText(QString text);
    void statusChanged(int wpm, int pitchHz);

private:
    void reset(float sampleRate);
    void processBlock();
    void keyStateChanged(bool down);
    void tickSpace();
    void classifyMark(float ms);
    void flushCharacter();
    float unitMs() const;

    std::atomic<bool> m_enabled { false };
    std::atomic<bool> m_resetPending { true };

    // Input / decimation
    float m_sampleRate = 0.0f;
    int   m_decim = 1;
    int   m_decimCount = 0;
    float m_decimAcc = 0.0f;
    float m_fs = 8000.0f;          // decimated rate
    int   m_blockLen = 64;
    float m_blockMs = 8.0f;
    std::vector<float> m_block;

    // Goertzel bank
    std::vector<float> m_freqs;
    std::vector<float> m_coeffs;
    std::vector<float> m_avgPower;
    int   m_pitchBin = 0;

    // Envelope
    float m_mag = 0.0f;
    float m_signal = 0.0f;
    float m_noise = 0.0f;          // mean magnitude while key-up
    bool  m_keyDown = false;

    // Timing (ms)
    float m_markMs = 0.0f;
    float m_spaceMs = 0.0f;
    float m_pendingMarkMs = 0.0f;
    float m_avgDot = 60.0f;        // 20 WPM
    float m_avgDash = 180.0f;
    bool  m_wordGapSent = true;
    QString m_symbol;
    std::deque<float> m_marks;     // recent mark lengths for speed tracking

    float m_statusMs = 0.0f;
};

#endif // CWDECODER_H
