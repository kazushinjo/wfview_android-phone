#include "cwdecoder.h"

#include <QHash>
#include <cmath>
#include <algorithm>

namespace {

const QHash<QString, QString> &morseTable()
{
    static const QHash<QString, QString> table = {
        {".-", "A"},    {"-...", "B"},  {"-.-.", "C"},  {"-..", "D"},
        {".", "E"},     {"..-.", "F"},  {"--.", "G"},   {"....", "H"},
        {"..", "I"},    {".---", "J"},  {"-.-", "K"},   {".-..", "L"},
        {"--", "M"},    {"-.", "N"},    {"---", "O"},   {".--.", "P"},
        {"--.-", "Q"},  {".-.", "R"},   {"...", "S"},   {"-", "T"},
        {"..-", "U"},   {"...-", "V"},  {".--", "W"},   {"-..-", "X"},
        {"-.--", "Y"},  {"--..", "Z"},
        {"-----", "0"}, {".----", "1"}, {"..---", "2"}, {"...--", "3"},
        {"....-", "4"}, {".....", "5"}, {"-....", "6"}, {"--...", "7"},
        {"---..", "8"}, {"----.", "9"},
        {".-.-.-", "."}, {"--..--", ","}, {"..--..", "?"}, {"-..-.", "/"},
        {"-...-", "="},  {"-....-", "-"}, {"-.--.", "("},  {"-.--.-", ")"},
        {".----.", "'"}, {"---...", ":"}, {".-..-.", "\""}, {".--.-.", "@"},
        {".-.-.", "+"},  {"-.-.--", "!"}, {"-.-.-.", ";"},
        {"...-.-", "<SK>"}, {"-.-.-", "<KA>"}, {"........", "<HH>"},
    };
    return table;
}

constexpr float kMinFreq = 300.0f;
constexpr float kMaxFreq = 1200.0f;
constexpr float kFreqStep = 50.0f;

} // namespace

CwDecoder::CwDecoder(QObject *parent)
    : QObject(parent)
{
}

void CwDecoder::setEnabled(bool en)
{
    if (en && !m_enabled.load(std::memory_order_relaxed))
        m_resetPending.store(true, std::memory_order_release);
    m_enabled.store(en, std::memory_order_release);
}

void CwDecoder::reset(float sampleRate)
{
    m_sampleRate = sampleRate;
    m_decim = std::max(1, static_cast<int>(std::lround(sampleRate / 8000.0f)));
    m_fs = sampleRate / m_decim;
    m_blockLen = std::max(16, static_cast<int>(std::lround(m_fs * 0.008f)));
    m_blockMs = 1000.0f * m_blockLen / m_fs;
    m_decimCount = 0;
    m_decimAcc = 0.0f;
    m_block.clear();
    m_block.reserve(m_blockLen);

    m_freqs.clear();
    m_coeffs.clear();
    for (float f = kMinFreq; f <= kMaxFreq + 0.1f; f += kFreqStep) {
        m_freqs.push_back(f);
        m_coeffs.push_back(2.0f * std::cos(2.0f * float(M_PI) * f / m_fs));
    }
    m_avgPower.assign(m_freqs.size(), 0.0f);
    m_pitchBin = static_cast<int>((600.0f - kMinFreq) / kFreqStep);

    m_mag = m_signal = m_noise = 0.0f;
    m_keyDown = false;
    m_markMs = m_spaceMs = m_pendingMarkMs = 0.0f;
    m_avgDot = 60.0f;
    m_avgDash = 180.0f;
    m_wordGapSent = true;
    m_symbol.clear();
    m_marks.clear();
    m_statusMs = 0.0f;
}

void CwDecoder::process(const float *samples, int count, float sampleRate, int channels)
{
    if (!m_enabled.load(std::memory_order_acquire) || count <= 0 || sampleRate <= 0.0f)
        return;
    if (m_resetPending.exchange(false, std::memory_order_acq_rel) || sampleRate != m_sampleRate)
        reset(sampleRate);

    const int step = (channels == 2) ? 2 : 1;
    for (int i = 0; i + step - 1 < count; i += step) {
        const float s = (step == 2) ? 0.5f * (samples[i] + samples[i + 1]) : samples[i];
        m_decimAcc += s;
        if (++m_decimCount < m_decim)
            continue;
        m_block.push_back(m_decimAcc / m_decim);
        m_decimAcc = 0.0f;
        m_decimCount = 0;
        if (static_cast<int>(m_block.size()) >= m_blockLen) {
            processBlock();
            m_block.clear();
        }
    }
}

void CwDecoder::processBlock()
{
    // Goertzel power at every candidate pitch.
    const int nBins = static_cast<int>(m_freqs.size());
    std::vector<float> power(nBins);
    for (int k = 0; k < nBins; ++k) {
        float s1 = 0.0f, s2 = 0.0f;
        const float c = m_coeffs[k];
        for (float x : m_block) {
            const float s0 = x + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        power[k] = std::max(0.0f, s1 * s1 + s2 * s2 - c * s1 * s2);
    }

    // Track the pitch: slow average per bin, switch only on a clear winner.
    int best = m_pitchBin;
    for (int k = 0; k < nBins; ++k) {
        m_avgPower[k] += 0.02f * (power[k] - m_avgPower[k]);
        if (m_avgPower[k] > m_avgPower[best])
            best = k;
    }
    if (best != m_pitchBin && m_avgPower[best] > 1.5f * m_avgPower[m_pitchBin])
        m_pitchBin = best;

    // Envelope at the pitch (normalised magnitude).
    const float mag = std::sqrt(power[m_pitchBin]) / m_blockLen;
    m_mag += 0.6f * (mag - m_mag);

    // Signal: peak follower (fast attack, slow decay).  Noise: mean magnitude
    // of the key-up blocks (edges of marks excluded), so the squelch sits
    // well above random noise peaks.
    const float range = m_signal - m_noise;
    const bool squelched = !(m_signal > 2.5f * m_noise && range > 0.0f);
    if (m_mag > m_signal)
        m_signal += 0.3f * (m_mag - m_signal);
    else
        m_signal += 0.004f * (m_mag - m_signal);
    if (m_noise <= 0.0f)
        m_noise = m_mag;
    else if (squelched || (!m_keyDown && m_mag < m_noise + 0.3f * range))
        m_noise += 0.01f * (m_mag - m_noise);

    bool down = false;
    if (!squelched) {
        down = m_keyDown ? m_mag > m_noise + 0.35f * range
                         : m_mag > m_noise + 0.5f * range;
    }

    if (down != m_keyDown)
        keyStateChanged(down);
    if (m_keyDown)
        m_markMs += m_blockMs;
    else
        tickSpace();

    m_statusMs += m_blockMs;
    if (m_statusMs >= 1000.0f) {
        m_statusMs = 0.0f;
        emit statusChanged(static_cast<int>(std::lround(1200.0f / unitMs())),
                           static_cast<int>(m_freqs[m_pitchBin]));
    }
}

float CwDecoder::unitMs() const
{
    return 0.5f * (m_avgDot + m_avgDash / 3.0f);
}

void CwDecoder::keyStateChanged(bool down)
{
    m_keyDown = down;
    if (down) {
        // A gap shorter than the glitch limit joins the two marks together.
        m_markMs = (m_pendingMarkMs > 0.0f) ? m_pendingMarkMs + m_spaceMs : 0.0f;
        m_pendingMarkMs = 0.0f;
        m_spaceMs = 0.0f;
    } else {
        m_pendingMarkMs = m_markMs;
        m_markMs = 0.0f;
        m_spaceMs = 0.0f;
    }
}

void CwDecoder::tickSpace()
{
    m_spaceMs += m_blockMs;
    const float unit = unitMs();

    if (m_pendingMarkMs > 0.0f && m_spaceMs >= std::max(12.0f, 0.4f * m_avgDot)) {
        classifyMark(m_pendingMarkMs);
        m_pendingMarkMs = 0.0f;
    }
    if (!m_symbol.isEmpty() && m_spaceMs >= 2.0f * unit)
        flushCharacter();
    if (!m_wordGapSent && m_spaceMs >= 5.0f * unit) {
        m_wordGapSent = true;
        emit decodedText(QStringLiteral(" "));
    }
}

void CwDecoder::classifyMark(float ms)
{
    // Ignore clicks and long carriers (tuning, etc.).
    if (ms < std::max(10.0f, 0.3f * m_avgDot))
        return;
    if (ms > 3.0f * m_avgDash && ms > 400.0f) {
        m_symbol.clear();
        return;
    }

    // Speed tracking: split the recent marks at the largest jump in length.
    // A jump of 1.8x or more separates dots from dashes.
    m_marks.push_back(ms);
    if (m_marks.size() > 24)
        m_marks.pop_front();
    if (m_marks.size() >= 4) {
        std::vector<float> s(m_marks.begin(), m_marks.end());
        std::sort(s.begin(), s.end());
        size_t split = 0;
        float bestRatio = 1.8f;
        for (size_t i = 1; i < s.size(); ++i) {
            const float r = s[i] / s[i - 1];
            if (r >= bestRatio) {
                bestRatio = r;
                split = i;
            }
        }
        if (split > 0) {
            float dot = 0.0f, dash = 0.0f;
            for (size_t i = 0; i < split; ++i) dot += s[i];
            for (size_t i = split; i < s.size(); ++i) dash += s[i];
            m_avgDot = dot / split;
            m_avgDash = dash / (s.size() - split);
        }
    }
    m_avgDot = std::clamp(m_avgDot, 15.0f, 400.0f);
    m_avgDash = std::clamp(m_avgDash, 2.0f * m_avgDot, 1200.0f);

    if (ms < std::sqrt(m_avgDot * m_avgDash))
        m_symbol += QLatin1Char('.');
    else
        m_symbol += QLatin1Char('-');

    if (m_symbol.size() > 8)
        m_symbol.clear();
}

void CwDecoder::flushCharacter()
{
    const auto &table = morseTable();
    const auto it = table.constFind(m_symbol);
    emit decodedText(it != table.constEnd() ? it.value() : QStringLiteral("*"));
    m_symbol.clear();
    m_wordGapSent = false;
}
