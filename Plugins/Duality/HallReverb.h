#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <vector>
#include <cmath>

// Warm, open hall: early reflections into an 8-line feedback delay network (FDN).
// - Hadamard mixing spreads energy evenly so the tail is dense instead of ringing.
// - Each line's delay time drifts slowly (chorus-like), which breaks up metallic resonances.
// - In-loop damping makes highs die faster than mids (warm), and a gentle low cut keeps lows from booming.
class HallReverb
{
public:
    static constexpr int N = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double k = sampleRate / 48000.0;
        const int maxLine = (int) std::ceil (3600 * 1.5 * k) + 64;
        for (auto& l : lines) l.setSize (maxLine);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4; ++i)
                diffusers[c][i].setSize ((int) std::round (diffLen[c][i] * k));
        erLine.setSize ((int) std::ceil (0.12 * sampleRate) + 8);
        reset();
        setParameters (0.7f, 0.4f);
    }

    void reset()
    {
        for (auto& l : lines) l.clear();
        for (auto& c : diffusers) for (auto& d : c) d.clear();
        erLine.clear();
        lp.fill (0.0f); hp.fill (0.0f);
        for (int i = 0; i < N; ++i) phase[i] = (double) i / N * juce::MathConstants<double>::twoPi;
    }

    // size 0..1: room size and decay time (about 0.9 s .. 6 s). damping 0..1: how fast highs die away.
    void setParameters (float size, float damping)
    {
        size = juce::jlimit (0.0f, 1.0f, size);
        damping = juce::jlimit (0.0f, 1.0f, damping);
        if (std::abs (size - lastSize) < 1.0e-4f && std::abs (damping - lastDamp) < 1.0e-4f)
            return;
        lastSize = size; lastDamp = damping;

        const double k = sr / 48000.0;
        const double scale = 0.55 + 0.95 * size;                  // room dimensions
        const double rt60 = 0.9 * std::pow (6.6, (double) size);   // seconds
        for (int i = 0; i < N; ++i)
        {
            baseLen[i] = (float) (lineLen[i] * scale * k);
            gain[i] = (float) std::pow (10.0, -3.0 * baseLen[i] / (rt60 * sr));
        }
        // in-loop one-pole lowpass: 12 kHz (bright) .. 3 kHz (dark)
        const double fc = 12000.0 * std::pow (0.25, (double) damping);
        lpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (fc, sr * 0.45) / sr));
        hpCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 70.0 / sr));
        erScale = (float) (0.6 + 0.8 * size);
    }

    void process (float* left, float* right, int numSamples)
    {
        const double k = sr / 48000.0;
        const float depth = (float) (9.0 * k);   // samples of slow delay-time drift

        for (int n = 0; n < numSamples; ++n)
        {
            // Input diffusion: four allpasses per side smear the attack into a dense wash
            float inL = left[n], inR = right[n];
            for (int i = 0; i < 4; ++i)
            {
                inL = diffusers[0][i].allpass (inL, 0.62f);
                inR = diffusers[1][i].allpass (inR, 0.62f);
            }

            // Early reflections: a few sparse taps that give the sense of a room
            erLine.push (0.5f * (left[n] + right[n]));
            float erL = 0.0f, erR = 0.0f;
            for (int t = 0; t < 6; ++t)
            {
                const int dl = (int) (erTapsL[t] * erScale * sr), dr = (int) (erTapsR[t] * erScale * sr);
                erL += erLine.tap (dl) * erGains[t];
                erR += erLine.tap (dr) * erGains[t];
            }

            // Read the delay lines (fractional, slowly drifting)
            std::array<float, N> y {};
            for (int i = 0; i < N; ++i)
            {
                phase[i] += juce::MathConstants<double>::twoPi * lfoRate[i] / sr;
                if (phase[i] > juce::MathConstants<double>::twoPi) phase[i] -= juce::MathConstants<double>::twoPi;
                const float d = baseLen[i] + depth * (float) std::sin (phase[i]);
                float v = lines[i].tapFrac (d) * gain[i];
                lp[i] += lpCoef * (v - lp[i]);   // highs decay faster
                v = lp[i];
                hp[i] += hpCoef * (v - hp[i]);   // keep the lows from building up
                y[i] = v - hp[i] * 0.6f;
            }

            // Hadamard mix (orthogonal, energy preserving)
            std::array<float, N> m = y;
            for (int len = 1; len < N; len <<= 1)
                for (int i = 0; i < N; i += len << 1)
                    for (int j = i; j < i + len; ++j)
                    {
                        const float a = m[(size_t) j], b = m[(size_t) (j + len)];
                        m[(size_t) j] = a + b;
                        m[(size_t) (j + len)] = a - b;
                    }
            const float norm = 1.0f / std::sqrt ((float) N);

            for (int i = 0; i < N; ++i)
                lines[i].push (m[(size_t) i] * norm + (i % 2 == 0 ? inL : inR) * 0.5f);

            // Stereo out: alternate lines to each side, with signs for decorrelation
            const float lateL = y[0] - y[2] + y[4] - y[6] + 0.5f * (y[1] - y[5]);
            const float lateR = y[1] - y[3] + y[5] - y[7] + 0.5f * (y[2] - y[6]);
            left[n]  = (lateL * 0.55f + erL * 0.35f) * outGain;
            right[n] = (lateR * 0.55f + erR * 0.35f) * outGain;
        }
    }

private:
    struct Line
    {
        std::vector<float> buf;
        int w = 0;
        void setSize (int n) { buf.assign ((size_t) juce::jmax (4, n), 0.0f); w = 0; }
        void clear()         { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        int size() const     { return (int) buf.size(); }
        void push (float x)  { buf[(size_t) w] = x; w = (w + 1) % size(); }
        float tap (int d) const
        {
            int i = w - 1 - juce::jlimit (0, size() - 1, d);
            if (i < 0) i += size();
            return buf[(size_t) i];
        }
        float tapFrac (float d) const
        {
            d = juce::jlimit (1.0f, (float) size() - 3.0f, d);
            const int di = (int) d;
            const float f = d - (float) di;
            return tap (di) * (1.0f - f) + tap (di + 1) * f;
        }
        float allpass (float x, float g)
        {
            const float delayed = tap (size() - 1);
            const float v = x + g * delayed;
            push (v);
            return delayed - g * v;
        }
    };

    double sr = 48000.0;
    std::array<Line, N> lines;
    Line diffusers[2][4];
    Line erLine;

    // mutually prime-ish lengths at 48 kHz (about 21..75 ms before scaling)
    static constexpr float lineLen[N] = { 1009.0f, 1361.0f, 1693.0f, 1999.0f, 2333.0f, 2683.0f, 3049.0f, 3571.0f };
    static constexpr float lfoRate[N] = { 0.31f, 0.37f, 0.43f, 0.29f, 0.53f, 0.41f, 0.47f, 0.35f };
    static constexpr int diffLen[2][4] = { { 142, 379, 107, 277 }, { 151, 389, 113, 263 } };
    static constexpr float erTapsL[6] = { 0.0113f, 0.0197f, 0.0291f, 0.0437f, 0.0613f, 0.0821f };
    static constexpr float erTapsR[6] = { 0.0127f, 0.0223f, 0.0311f, 0.0467f, 0.0659f, 0.0887f };
    static constexpr float erGains[6] = { 0.55f, -0.45f, 0.38f, -0.30f, 0.24f, -0.18f };

    std::array<float, N> baseLen {}, gain {}, lp {}, hp {};
    double phase[N] {};
    float lpCoef = 0.5f, hpCoef = 0.01f, erScale = 1.0f, lastSize = -1.0f, lastDamp = -1.0f;
    float outGain = 1.0f;

public:
    void setOutputGain (float g) { outGain = g; }
};
