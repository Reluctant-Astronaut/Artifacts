# Artifacts: test report

Plugins: Console RA-1, Kintsugi, Duality, Anemoia, Equanimity (v1.0.0, JUCE 8.0.9). Tested on Linux x86-64, VST3, at 44.1, 48 and 96 kHz, stereo.
Raw numbers: `reports/data/*.tsv` (one row per measurement: plugin, test, metric, value, verdict). Spectra: `reports/data/*.csv`. Plots: `reports/img/`.

## 1. pluginval (strictness 10, in-process, GUI tests skipped)
| Plugin | Result |
|---|---|
| Console RA-1 | SUCCESS |
| Kintsugi | SUCCESS |
| Duality | SUCCESS |
| Anemoia | SUCCESS |
| Equanimity | SUCCESS |

pluginval found two real bugs in Anemoia on the way (its in/out switches did not restore from saved state); fixed by making them two-position choice switches like the other plugins.

## 2. Sonic tests: summary
Verdict counts from the final run (PASS / WARN / FAIL; INFO rows are plain measurements):

| Plugin | PASS | WARN | FAIL |
|---|---|---|---|
| Console RA-1 | 81 | 4 | 0 |
| Kintsugi | 80 | 4 | 1 |
| Duality | 76 | 2 | 0 |
| Anemoia | 58 | 0 | 0 |
| Equanimity | 72 | 0 | 0 |

Every WARN and the one FAIL are at the "driven" setting (a knob at 60 %, signal at -6 dBFS): they are the character of working the plugin hard, not faults. Details in section 5.

## 3. What the tests check, and what they found at default settings

**Latency and alignment.** Reported latency matches measured: Console RA-1 1.27 ms (mid-band error 0.02 samples), Kintsugi 11.25 ms (0.45 samples: the tape transport). Mix at 50 % has no comb filtering (worst dip -0.11 dB and -0.50 dB over 100 Hz-12 kHz), so parallel use and Mix are safe. Duality and Anemoia: zero latency.

**Nulling.** Mix at 0 % returns the dry signal delayed by the reported latency, bit-exact for both strips. Mix sweeps do not click.

**Phase.** Console RA-1: 6 deg at 40 Hz, -5 deg at 16 kHz. Kintsugi: 33 deg at 40 Hz (head bump and low-end loss, like real tape), -51 deg at 16 kHz. Gentle, no polarity flips, strong correlation with the source. Mono fold-down of both strips loses under 0.5 dB in every octave. The Duality reverbs and Anemoia keep mono-fold loss within about 4.5 dB per octave (typical of wide decorrelated tails) and no negative L/R correlation.

**Harmonics (1 kHz sine).** The harmonic ladder falls steeply: higher harmonics (5th-12th) are about 20-30 dB under the 3rd.
| Level | Console RA-1 THD (H2 / H3) | Kintsugi THD (H2 / H3) |
|---|---|---|
| -24 dBFS | 0.03 % (-70 / -117 dB) | 0.18 % (-55 / -68 dB) |
| -12 dBFS | 0.12 % (-59 / -68 dB) | 0.94 % (-43 / -44 dB) |
| -6 dBFS | 0.65 % (-54 / -44 dB) | 2.6 % (-38 / -33 dB) |
Kintsugi adds both even and odd harmonics early, which is the "thick" part; Console stays clean until pushed.

**Aliasing** (8.16 kHz sine; everything audible that is not a true harmonic): Console RA-1 below -124 dB at default; Kintsugi about -90 dB at default (-92 dB at -3 dBFS). Pushed to 60 %: -93 dB (Console) and -78 dB (Kintsugi). Anemoia pitch-shifted grains (+12 and +7 semitones, 8 kHz source): spurs under -77 dB. Components above 20 kHz are reported separately and are inaudible.

**Tone balance** (pink noise in vs out, octave bands): both strips are flat to within 0.1 dB between 2-8 kHz versus the mids (no harshness added), no loss of low-mid body (thinness within -0.12 dB), 16 kHz within 0.3-0.4 dB, level change under 0.8 dB. The Duality tails get darker over time (a real room's air absorption), never brighter. The spectral fluctuation of the tails is 5.6-6.5 dB, which is what a dense diffuse tail measures (Room 7.2 dB: see limits).

**Duality loudness across modes.** After level matching, Room / Hall / Plate / Subject / Kensho sit within 2 dB of each other at default (they were 20 dB apart in the first run: the IR path was 19 dB too quiet and is now compensated).

**Kensho.** Reverb section and IR section run together and are fused through one shared all-pass diffusion stage. An earlier version summed the dry and diffused signals, which made a comb filter (spectral fluctuation 8.3 dB); now a pure all-pass chain: 5.6 dB, as smooth as either reverb alone.

**Anemoia.** Echo time 400.17 ms for a 400 ms setting, 250.10 ms for 1/8 at 120 bpm, feedback repeats decay as set, ping-pong alternates left, right, left. Silence in gives silence out (no noise, no DC). At maximum Feedback and Bloom for 30 seconds with loud noise: no runaway, the level settles (last-5-second RMS about -2 dBFS) and everything decays to under -64 dBFS after the input stops. Grain pitch shifting is anti-aliased with a band-limited copy of the memory.

**Robustness.** Random and extreme parameter sets while playing and jumping knobs: no NaN/Inf, no denormal trouble, no clicks (largest sample step well inside limits). Block sizes 7, 64 and 512 give the same audio (Duality -80 dB from the convolution engine's rounding).

## 4. What was fixed because of the tests
1. Duality: IR path 19 dB quieter than the algorithmic reverbs; now level matched.
2. Duality Kensho: comb-filter colouration from summing the diffused signal; now a pure all-pass fusion.
3. Duality Room: added input diffusion so the small space rings less.
4. Anemoia: switch state did not restore from saved sessions (pluginval).
5. Test rig: found and removed false alarms (random per-instance unit tolerance in the strips, asynchronous IR loading, an impulse-peak latency estimate), so the remaining numbers are trustworthy.

## 5. Honest limits
- **Instance personality is by design.** Every Kintsugi and Console instance picks its own "unit" (about +-0.1 dB balance, a fraction of a sample azimuth, small filter tolerances), like real hardware, and it is saved with the project. A mono source therefore comes out very slightly wide (Console about -42 dB side/mid, Kintsugi about -26 dB).
- **Driven settings add real distortion.** At a knob setting of 60 %, a -6 dBFS tone gives 13 % THD in both strips, with 5th-12th harmonics 37-41 dB under the fundamental. That is saturation, not a defect, but it is the point where it will start to sound edgy if you stack several. Maximum drive is even harder; it was excluded from pass/fail.
- **Duality Room** is the least smooth tail (7.2 dB fluctuation against 5.6-6.5 for the others); small rooms are naturally more modal.
- **Duality Plate** is wide: its mono fold loses 4.6 dB in the worst octave.
- **Anemoia at maximum Feedback with Bloom up** can peak around +11 dBFS on loud material. It is soft-limited and stable, but lower the Output.
- **Not covered:** AU and macOS/Windows builds (they only build through the GitHub workflow), and pluginval's GUI tests. All tests are measurements; nobody has yet listened to these builds in Cubase. The warm/lush/organic goal is supported by the numbers above, but only your ears can confirm it.

## 6. Equanimity (Pultec EQP-1A)
Checked against the published EQP-1A specification (EQ in, reference +10 dBm taken as -12 dBFS):
- Low boost, 20/30/60/100 Hz, 0-13.5 dB: 13.1 / 13.6 / 13.8 / 13.9 dB at full boost.
- Low atten, 0-17.5 dB: 17.9 / 17.5 / 17.2 / 17.1 dB at full atten.
- High boost, 3-16 kHz, 0-18 dB: 17.96-17.97 dB at the centre frequency, every step. High atten 5 / 10 kHz: 15.9 / 15.7 dB at 20 kHz; the 20 kHz shelf reaches 8 dB at 20 kHz (a shelf at the band edge cannot do more).
- Amplifier flat 20 Hz-20 kHz: within 0.2 dB (spec +/-0.5 dB). EQ IN out: boosted settings have no effect (< 0.02 dB) and the tube stage stays in circuit.
- Distortion at the reference level (-12 dBFS): 0.14-0.15 % (spec <= 0.15 %). Colour grows with level: ~0.5 % at -6 dBFS, ~2.7 % at 0 dBFS. Worst alias spur at full level: -122 dBc (4x oversampling).
- The low-end trick (boost + atten at the same frequency) gives a bump below and a dip above the chosen frequency (+4.6 dB / -4.4 dB at boost 6, atten 3, 60 Hz).
- Path: Mid and Side banks act on their own channel only (the other channel moves less than 0.05 dB); L/R ignores the M/S banks; Solo passes only Mid or only Side; block-size independent (bit-identical at 64 and 1024).
- Midrange (MEQ-5 style): low peak +10 dB (200 Hz-1 kHz), dip -10 dB (200 Hz-5 kHz), high peak +10 dB (1.5-5 kHz): all within 0.01 dB at their centres; the Mid/Side banks carry their own midrange settings.
- Auto gain: with pink noise, bass boost, air boost, mid dip, high cut and a mixed setting all stay within about 1 dB of the unprocessed level (off, they move by -1.7 to +7 dB); a flat EQ is untouched.
Not modelled: the 600 ohm / 50 ohm impedances and the hardware noise floor (92 dB below +10 dBm); the plugin is digital-quiet.
The EQ is a coupled set of biquads (resonant boost shelf and gentle atten shelf that give way to each other, three midrange bells, peak, high shelf) tuned to the hardware curves, not a component-level circuit simulation.
