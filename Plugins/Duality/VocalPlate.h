#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <array>
#include <cmath>

// The vocal plate. A figure-eight tank after Jon Dattorro ("Effect Design Part 1", JAES 1997), voiced as an EMT 140:
//  - the decay is set in seconds (RT60) and holds at every setting, as on the plate's damper;
//  - the lows die a little faster than the mids, as on a real steel plate (no mud under the voice);
//  - Damping darkens the tail inside the tank, so it darkens as it decays, not all at once;
//  - the tank's allpasses drift slowly and gently: lush, without the seasick pitch wobble that ruins a vocal;
//  - mono in, stereo out, like the plate's two pickups.
// Freeze holds the tail as it is (no new input, no loss): used by the throw plate.
class VocalPlate
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const double k = sampleRate / 29761.0;   // the paper gives its lengths at 29761 Hz
        auto n = [k] (int samples) { return juce::jmax (1, (int) std::round (samples * k)); };

        inDiff[0].setSize (n (142)); inDiff[1].setSize (n (107));
        inDiff[2].setSize (n (379)); inDiff[3].setSize (n (277));

        excursion = (float) (14.0 * k);   // enough drift to break up the ringing of the tank, not so much that a vocal wobbles
        modAP[0].setSize (n (672) + (int) excursion + 4);  baseMod[0] = (float) n (672);
        modAP[1].setSize (n (908) + (int) excursion + 4);  baseMod[1] = (float) n (908);
        delayA[0].setSize (n (4453)); delayA[1].setSize (n (4217));
        decayAP[0].setSize (n (1800)); decayAP[1].setSize (n (2656));
        delayB[0].setSize (n (3720)); delayB[1].setSize (n (3163));
        // the time a sound takes through each half of the tank (it passes the decay gain twice on the way)
        for (int s = 0; s < 2; ++s)
            halfLoop[s] = (baseMod[s] + (float) delayA[s].size() + (float) decayAP[s].size() + (float) delayB[s].size()) / (float) sr;

        tL = { n (266), n (2974), n (1913), n (1996), n (1990), n (187), n (1066) };
        tR = { n (353), n (3627), n (1228), n (2673), n (2111), n (335), n (121) };

        // the plate's thin low end: a gentle high-pass inside the tank, so the lows decay faster than the mids
        airCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jmin (11000.0, sr * 0.45) / sr);
        lowLeak = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 110.0 / sr));
        reset();
        setParameters (2.0f, 0.3f);
    }

    void reset()
    {
        for (auto* l : { &inDiff[0], &inDiff[1], &inDiff[2], &inDiff[3], &modAP[0], &modAP[1],
                         &delayA[0], &delayA[1], &decayAP[0], &decayAP[1], &delayB[0], &delayB[1] })
            l->clear();
        air[0] = air[1] = damp[0] = damp[1] = tankFeed[0] = tankFeed[1] = low[0] = low[1] = 0.0f;
        lfo[0] = 0.0; lfo[1] = 1.7; lfo[2] = 0.6; lfo[3] = 2.9;
    }

    // decay: RT60 in seconds (at about 1 kHz); damping 0..1: from open and silvery to dark and warm
    void setParameters (float decaySeconds, float damping)
    {
        rt60 = juce::jlimit (0.2f, 30.0f, decaySeconds);
        // measured against the knob: the tank's low leak and its diffusion shift the real decay a little; this puts it back
        rt60 /= juce::jlimit (0.6f, 1.3f, 1.06f - 0.13f * std::log (rt60));
        for (int s = 0; s < 2; ++s)
            decay[s] = (float) std::pow (10.0, -3.0 * (double) halfLoop[s] / (2.0 * (double) rt60));
        damping = juce::jlimit (0.0f, 1.0f, damping);
        const double fc = juce::jmin (9000.0 * std::pow (1800.0 / 9000.0, (double) damping), sr * 0.45);   // 9 kHz open, 1.8 kHz dark
        dampCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * fc / sr);
    }

    void setFreeze (bool f) { freeze = f; }

    // mono send in (already pre-delayed and filtered), stereo wet out
    void process (const float* in, float* outL, float* outR, int numSamples)
    {
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double inc[4] = { twoPi * 0.31 / sr, twoPi * 0.47 / sr, twoPi * 0.23 / sr, twoPi * 0.61 / sr };
        const float dk[2] = { freeze ? 1.0f : decay[0], freeze ? 1.0f : decay[1] };
        const float inGain = freeze ? 0.0f : 1.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float x = in[i] * inGain;
            x = inDiff[0].allpass (x, 0.75f);
            x = inDiff[1].allpass (x, 0.75f);
            x = inDiff[2].allpass (x, 0.625f);
            x = inDiff[3].allpass (x, 0.625f);

            for (int j = 0; j < 4; ++j) { lfo[j] += inc[j]; if (lfo[j] > twoPi) lfo[j] -= twoPi; }
            // two slow, unrelated movements per allpass: it drifts rather than wobbles
            const float mod0 = excursion * (float) (0.65 * std::sin (lfo[0]) + 0.35 * std::sin (lfo[2]));
            const float mod1 = excursion * (float) (0.65 * std::sin (lfo[1]) + 0.35 * std::sin (lfo[3]));

            for (int s = 0; s < 2; ++s)
            {
                float v = x + tankFeed[1 - s] * dk[s];
                v = modAP[s].allpassFrac (v, -0.70f, baseMod[s] + (s == 0 ? mod0 : mod1));
                delayA[s].push (v);
                float d = delayA[s].tap (delayA[s].size() - 1);
                if (! freeze)
                {
                    damp[s] += (1.0f - dampCoef) * (d - damp[s]);   // highs fade inside the tank
                    air[s] += (1.0f - airCoef) * (damp[s] - air[s]);  // and the very top faster still, as on a steel plate:
                                                                       // the hiss of an S must not ring on in the tail
                    low[s] += lowLeak * (air[s] - low[s]);
                    d = air[s] - 0.08f * low[s];                    // and the lows a little too: a plate is lean underneath
                }
                d *= dk[s];
                d = decayAP[s].allpass (d, 0.50f);
                delayB[s].push (d);
                tankFeed[s] = delayB[s].tap (delayB[s].size() - 1);
            }

            const float yl = delayA[1].tap (tL[0]) + delayA[1].tap (tL[1]) - decayAP[1].tap (tL[2]) + delayB[1].tap (tL[3])
                           - delayA[0].tap (tL[4]) - decayAP[0].tap (tL[5]) - delayB[0].tap (tL[6]);
            const float yr = delayA[0].tap (tR[0]) + delayA[0].tap (tR[1]) - decayAP[0].tap (tR[2]) + delayB[0].tap (tR[3])
                           - delayA[1].tap (tR[4]) - decayAP[1].tap (tR[5]) - delayB[1].tap (tR[6]);
            outL[i] = yl * outGain;
            outR[i] = yr * outGain;
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
        void push (float x)  { buf[(size_t) w] = x; if (++w >= size()) w = 0; }
        float tap (int d) const   // the value written d samples ago (0 = the latest)
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
            // cubic (Hermite) read: no dulling of the highs as the delay moves
            const float y0 = tap (di - 2), y1 = tap (di - 1), y2 = tap (di), y3 = tap (di + 1);
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            const float delayed = ((c3 * frac + c2) * frac + c1) * frac + y1;
            const float v = x + g * delayed;
            push (v);
            return delayed - g * v;
        }
    };

    double sr = 48000.0, lfo[4] {};
    Line inDiff[4], modAP[2], delayA[2], decayAP[2], delayB[2];
    float baseMod[2] {}, excursion = 12.0f, halfLoop[2] { 0.3f, 0.3f };
    std::array<int, 7> tL {}, tR {};
    float rt60 = 2.0f, decay[2] { 0.7f, 0.7f }, dampCoef = 0.3f, airCoef = 0.3f, damp[2] {}, air[2] {}, tankFeed[2] {}, low[2] {}, lowLeak = 0.01f;
    float outGain = 0.6f;
    bool freeze = false;
};
