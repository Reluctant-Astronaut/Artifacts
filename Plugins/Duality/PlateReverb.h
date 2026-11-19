#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <cmath>

// Plate reverb after Jon Dattorro, "Effect Design Part 1" (JAES 1997):
// input diffusion into a figure-eight "tank" of allpasses and delays, with the
// output taken from several points in the tank. Bright, dense and smooth: a plate.
class PlateReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double k = sampleRate / 29761.0;   // lengths in the paper are given at 29761 Hz
        auto n = [k] (int samples) { return juce::jmax (1, (int) std::round (samples * k)); };

        inDiff[0].setSize (n (142)); inDiff[1].setSize (n (107));
        inDiff[2].setSize (n (379)); inDiff[3].setSize (n (277));

        excursion = (float) (26.0 * k);   // deeper, slower movement: lush and not metallic
        modAP[0].setSize (n (672) + (int) excursion + 4);  baseMod[0] = (float) n (672);
        modAP[1].setSize (n (908) + (int) excursion + 4);  baseMod[1] = (float) n (908);
        delayA[0].setSize (n (4453)); delayA[1].setSize (n (4217));
        decayAP[0].setSize (n (1800)); decayAP[1].setSize (n (2656));
        delayB[0].setSize (n (3720)); delayB[1].setSize (n (3163));

        // output taps (left from mixed points around the tank, right likewise, mirrored)
        tL = { n (266), n (2974), n (1913), n (1996), n (1990), n (187), n (1066) };
        tR = { n (353), n (3627), n (1228), n (2673), n (2111), n (335), n (121) };

        reset();
    }

    void reset()
    {
        for (auto* l : { &inDiff[0], &inDiff[1], &inDiff[2], &inDiff[3], &modAP[0], &modAP[1],
                         &delayA[0], &delayA[1], &decayAP[0], &decayAP[1], &delayB[0], &delayB[1] })
            l->clear();
        bwState = damp[0] = damp[1] = tankFeed[0] = tankFeed[1] = 0.0f;
        lfoPhase = lfoPhase2 = 0.0;
    }

    // size 0..1 sets the decay (short plate .. huge, lush wash), damping 0..1 darkens the tail
    void setParameters (float size, float damping)
    {
        size = juce::jlimit (0.0f, 1.0f, size);
        damping = juce::jlimit (0.0f, 1.0f, damping);
        decay = 0.50f + 0.44f * size;
        // in-tank lowpass, from airy (13 kHz) to dark (3 kHz); the one-pole "keeps" (1 - dampCoef) of each change
        const double fc = 13000.0 * std::pow (0.23, (double) damping);
        dampCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (fc, sr * 0.45) / sr);
        // input bandwidth: plates are bright, but a gentle roll-off above ~9 kHz keeps them sweet
        bandwidth = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (9000.0, sr * 0.45) / sr));
    }

    void process (float* left, float* right, int numSamples)
    {
        const double lfoInc = juce::MathConstants<double>::twoPi * 0.55 / sr;
        const double lfoInc2 = juce::MathConstants<double>::twoPi * 0.73 / sr;

        for (int i = 0; i < numSamples; ++i)
        {
            // mono in, band-limited and diffused
            float x = 0.5f * (left[i] + right[i]);
            bwState += bandwidth * (x - bwState);
            x = bwState;
            x = inDiff[0].allpass (x, 0.75f);
            x = inDiff[1].allpass (x, 0.75f);
            x = inDiff[2].allpass (x, 0.625f);
            x = inDiff[3].allpass (x, 0.625f);

            // gently modulated tank allpasses keep the tail from ringing metallic
            lfoPhase += lfoInc;
            if (lfoPhase > juce::MathConstants<double>::twoPi) lfoPhase -= juce::MathConstants<double>::twoPi;
            lfoPhase2 += lfoInc2;
            if (lfoPhase2 > juce::MathConstants<double>::twoPi) lfoPhase2 -= juce::MathConstants<double>::twoPi;
            const float mod0 = excursion * (float) std::sin (lfoPhase);
            const float mod1 = excursion * (float) std::sin (lfoPhase2 + 1.3);

            for (int s = 0; s < 2; ++s)
            {
                float v = x + tankFeed[1 - s] * decay;
                v = modAP[s].allpassFrac (v, -0.70f, baseMod[s] + (s == 0 ? mod0 : mod1));
                delayA[s].push (v);
                float d = delayA[s].tap (delayA[s].size() - 1);
                damp[s] += (1.0f - dampCoef) * (d - damp[s]);
                d = damp[s] * decay;
                d = decayAP[s].allpass (d, 0.50f);
                delayB[s].push (d);
                tankFeed[s] = delayB[s].tap (delayB[s].size() - 1);
            }

            float yl = delayA[1].tap (tL[0]) + delayA[1].tap (tL[1]) - decayAP[1].tap (tL[2]) + delayB[1].tap (tL[3])
                     - delayA[0].tap (tL[4]) - decayAP[0].tap (tL[5]) - delayB[0].tap (tL[6]);
            float yr = delayA[0].tap (tR[0]) + delayA[0].tap (tR[1]) - decayAP[0].tap (tR[2]) + delayB[0].tap (tR[3])
                     - delayA[1].tap (tR[4]) - decayAP[1].tap (tR[5]) - delayB[1].tap (tR[6]);

            left[i]  = yl * outGain;
            right[i] = yr * outGain;
        }
    }

private:
    struct Line
    {
        std::vector<float> buf;
        int w = 0;

        void setSize (int n) { buf.assign ((size_t) juce::jmax (2, n), 0.0f); w = 0; }
        void clear()         { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        int size() const     { return (int) buf.size(); }
        void push (float x)  { buf[(size_t) w] = x; w = (w + 1) % size(); }

        // value written 'd' samples ago (0 = the latest)
        float tap (int d) const
        {
            int i = w - 1 - juce::jlimit (0, size() - 1, d);
            if (i < 0) i += size();
            return buf[(size_t) i];
        }

        float allpass (float x, float g)
        {
            const float delayed = tap (size() - 1);
            const float v = x + g * delayed;
            push (v);
            return delayed - g * v;
        }

        float allpassFrac (float x, float g, float delaySamples)
        {
            const float dd = juce::jlimit (1.0f, (float) size() - 2.0f, delaySamples);
            const int di = (int) dd;
            const float frac = dd - (float) di;
            const float delayed = tap (di - 1) * (1.0f - frac) + tap (di) * frac;
            const float v = x + g * delayed;
            push (v);
            return delayed - g * v;
        }
    };

    double sr = 44100.0, lfoPhase = 0.0, lfoPhase2 = 0.0;
    Line inDiff[4], modAP[2], delayA[2], decayAP[2], delayB[2];
    float baseMod[2] {}, excursion = 16.0f;
    std::array<int, 7> tL {}, tR {};
    float bandwidth = 0.9995f, bwState = 0.0f;
    float decay = 0.7f, dampCoef = 0.3f, damp[2] {}, tankFeed[2] {};
    float outGain = 0.6f;
};
