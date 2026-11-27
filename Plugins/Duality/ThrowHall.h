#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// The throw's reverb: not a second EMT. A throw should leave the plate behind and open into a larger, smoother space,
// so it is a hall: eight modulated delay lines mixed through a Householder matrix (a feedback delay network), after
// four short diffusers. The slow drift of every line keeps it from ringing metallic; its highs die sooner than its
// body, as in a real room. Mono in, stereo out.
class ThrowHall
{
public:
    void prepare (double sampleRate, float maxSize = 1.5f)
    {
        sr = sampleRate;
        const double k = sr / 1000.0;
        for (size_t i = 0; i < diff.size(); ++i)
        {
            diff[i].buf.assign ((size_t) std::lround (diffMs[i] * k) + 1, 0.0f);
            diff[i].w = 0;
        }
        for (size_t i = 0; i < N; ++i)
        {
            lines[i].buf.assign ((size_t) std::lround (lineMs[i] * k * maxSize) + 64, 0.0f);
            lines[i].w = 0;
            lines[i].lp = 0.0f;
            phase[i] = (float) i / (float) N * juce::MathConstants<float>::twoPi;
            rate[i] = (float) (juce::MathConstants<double>::twoPi * (0.21 + 0.067 * (double) i) / sr);
        }
        size = -1.0f;
        setSize (1.0f);
        setParameters (3.0f, 0.0f);
    }

    void reset()
    {
        for (auto& d : diff) std::fill (d.buf.begin(), d.buf.end(), 0.0f);
        for (auto& l : lines) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.lp = 0.0f; }
    }

    // Size 0.6 .. 1.5: the lengths of the lines (a larger room: later, sparser first reflections, a slower build)
    void setSize (float s)
    {
        if (std::abs (s - size) < 0.005f) return;
        size = s;
        for (size_t i = 0; i < N; ++i)
            len[i] = juce::jlimit (8.0f, (float) lines[i].buf.size() - 40.0f, (float) (lineMs[i] * sr / 1000.0 * size));
        updateGains();
    }

    // decay: RT60 in seconds; tone -1 (dark) .. +1 (bright): how fast the highs die against the body
    void setParameters (float decaySeconds, float tone)
    {
        if (std::abs (decaySeconds - rt) < 1.0e-4f && std::abs (tone - toneNow) < 1.0e-4f) return;
        rt = juce::jlimit (0.3f, 30.0f, decaySeconds);
        toneNow = tone;
        // the damping in each line: part of every pass goes through a gentle low-pass (at 5 kHz), more of it when dark
        // (a little at a time: the highs die sooner than the body, but the tail stays open and airy)
        dampCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 5000.0 / sr);
        hfShare = juce::jlimit (0.03f, 0.9f, 0.18f - 0.15f * tone + (tone < 0.0f ? -0.35f * tone : 0.0f));
        updateGains();
    }

    void process (const float* in, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            // diffuse the input: four allpasses
            float x = in[i];
            for (size_t d = 0; d < diff.size(); ++d)
            {
                auto& ap = diff[d];
                const int sz = (int) ap.buf.size();
                const float delayed = ap.buf[(size_t) ap.w];
                const float v = x + 0.62f * delayed;
                ap.buf[(size_t) ap.w] = v;
                x = delayed - 0.62f * v;
                if (++ap.w >= sz) ap.w = 0;
            }
            // read the lines (each drifting a few samples, slowly)
            std::array<float, N> y {};
            for (size_t l = 0; l < N; ++l)
            {
                phase[l] += rate[l];
                if (phase[l] > juce::MathConstants<float>::twoPi) phase[l] -= juce::MathConstants<float>::twoPi;
                const float d = len[l] + depth * std::sin (phase[l]);
                auto& line = lines[l];
                const int sz = (int) line.buf.size();
                float pos = (float) line.w - d;
                while (pos < 0) pos += (float) sz;
                const int i0 = (int) pos; const float f = pos - (float) i0;
                const int i1 = i0 + 1 >= sz ? 0 : i0 + 1;
                float v = line.buf[(size_t) i0] + f * (line.buf[(size_t) i1] - line.buf[(size_t) i0]);
                // the decay of this line, and its highs a little faster
                v *= gain[l];
                line.lp = v + dampCoef * (line.lp - v);
                y[l] = v + hfShare * (line.lp - v);
            }
            // the outputs: two different blends of the lines
            float l = 0, r = 0;
            for (size_t k = 0; k < N; ++k) { l += y[k] * outSignL[k]; r += y[k] * outSignR[k]; }
            outL[i] = l * 0.9f; outR[i] = r * 0.9f;
            // mix them (Householder: every line feeds every other) and write back with the input
            float sum = 0; for (float v : y) sum += v;
            const float h = sum * (2.0f / (float) N);
            for (size_t k = 0; k < N; ++k)
            {
                auto& line = lines[k];
                line.buf[(size_t) line.w] = (y[k] - h) + x * inSign[k] * 0.5f;
                if (++line.w >= (int) line.buf.size()) line.w = 0;
            }
        }
    }

private:
    static constexpr size_t N = 8;
    // (mutually prime-ish lengths, ms at Size 1: no two lines ring together)
    static constexpr double lineMs[N] { 41.3, 47.9, 55.7, 63.1, 71.9, 83.3, 94.7, 109.1 };
    static constexpr double diffMs[4] { 4.71, 6.37, 8.93, 12.07 };
    static constexpr float inSign[N]   { 1, -1, 1, -1, 1, -1, 1, -1 };
    static constexpr float outSignL[N] { 1, 1, -1, -1, 1, 1, -1, -1 };
    static constexpr float outSignR[N] { 1, -1, 1, -1, -1, 1, -1, 1 };

    void updateGains()
    {
        for (size_t i = 0; i < N; ++i)
            gain[i] = (float) std::pow (10.0, -3.0 * (double) len[i] / (sr * (double) rt));
    }

    struct Line { std::vector<float> buf; int w = 0; float lp = 0; };
    struct AP { std::vector<float> buf; int w = 0; };
    std::array<Line, N> lines;
    std::array<AP, 4> diff;
    std::array<float, N> len {}, gain {}, phase {}, rate {};
    double sr = 48000.0;
    float size = 1.0f, rt = 3.0f, toneNow = 0.0f, dampCoef = 0.5f;
    const float depth = 6.0f;      // samples of drift
    float hfShare = 0.3f;
};
