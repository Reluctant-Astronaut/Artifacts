#pragma once

#include "ArtifactsProcessor.h"
#ifndef ARTIFACTS_VARIANT
 #define ARTIFACTS_VARIANT 1
#endif

// Shared embedded fonts
struct KintsugiFonts
{
    static juce::Font serif (float height, bool bold = true);
    // at small window sizes the panel's writing is drawn a little larger than its share, so it stays readable
    static inline float boost = 1.0f;
};

class KintsugiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KintsugiLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    void drawPointerKnob (juce::Graphics&, int x, int y, int w, int h, float angle, juce::Slider&);
    void drawCreamKnob (juce::Graphics&, int x, int y, int w, int h, float angle, juce::Slider&);
    void drawKintsugiKnob (juce::Graphics&, int x, int y, int w, int h, float angle, juce::Slider&);
    void drawBrassKnob (juce::Graphics&, int x, int y, int w, int h, float angle, juce::Slider&);
    void drawNostalgiaKnob (juce::Graphics&, int x, int y, int w, int h, float angle, juce::Slider&);
    static constexpr bool useCreamKnobs = true;   // false = the silver spun-aluminium knobs
};

// Jewel pilot light that doubles as a section's IN switch. Glow 0 = dark, 1 = fully lit (amber).
class PilotLamp : public juce::Component, public juce::SettableTooltipClient
{
public:
    PilotLamp();
    void setGlow (float);
    void setRed (bool r) { red = r; repaint(); }
    void paint (juce::Graphics&) override;
private:
    float glow = 0.0f;
    bool red = false;
};

// A chrome bat-handle toggle switch, as on old consoles: lever up = in, lever down = out
class ToggleSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToggleSwitch();
    void setOn (bool);
    void setMiddle (bool m) { mid = m; repaint(); }   // three-way: lever upright but short
    void setMode (int m);                             // three-way switch: 0 up, 1 middle, 2 down
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void()> onClick;
private:
    bool on = true, mid = false;
};

// Moving-coil VU meter: warm-lit cream face, black needle with real needle ballistics
class VUMeter : public juce::Component
{
public:
    // variant 0: the original meter. variant 1: the replacement, a near-twin from a different maker
    VUMeter (juce::String channelLabel, int variant);
    void setMeanSquare (float ms) { target = ms; direct = false; }
    void setDirectVU (float vu) { directVU = vu; direct = true; }   // for a meter that shows something other than level (gain reduction)
    void tick (float dt);   // advance the needle physics
    void setEdgewise (bool e) { edge = e; renderFace(); }   // the horizontal edgewise face (Console)
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void renderFace();
    juce::String label;
    int variant = 0;
    bool edge = false;
    juce::Image face;
    float target = 0.0f, pos = 0.0f, vel = 0.0f, directVU = 0.0f;
    bool direct = false;
};

// A small window cut into the plate, with a numbered drum behind it (like an old tape counter).
// The drum rolls to the new number when the knob is turned.
class DrumWindow : public juce::Component
{
public:
    explicit DrumWindow (juce::StringArray items);
    void setIndex (int i) { target = (float) juce::jlimit (0, items.size() - 1, i); }
    void snap() { pos = target; repaint(); }
    void tick();
    void setText (const juce::String& t) { if (items.size() == 1 && items[0] != t) { items.set (0, t); repaint(); } }
    void paint (juce::Graphics&) override;
private:
    juce::StringArray items;
    float pos = 0.0f, target = 0.0f;
};

// A recessed cream window showing the name and decay of the loaded impulse response (preview only)
class IRWindow : public juce::Component, public juce::SettableTooltipClient
{
public:
    juce::String name, length { "2.41 s" }, rate { "48 kHz" }, channels { "STEREO" };
    std::vector<float> peaks;                 // the decay, 0..1 per bin (from the loaded impulse response)
    std::function<void()> onClick;            // click the window to choose another impulse response
    IRWindow() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void paint (juce::Graphics&) override;
};

// Duality: the Words window. The vocal's transcribed lyrics laid out along its waveform; words are selected like text
// and carry their effects (throw, echo, swell, double, harmony, shift), printed above them and drawn into the waveform.
class WordsWindow : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct Word { juce::String text; float t0, t1; juce::String tag; bool selected = false; };
    std::vector<Word> words;
    float length = 9.6f, playhead = 6.45f;
    juce::String source { juce::String::fromUTF8 ("LEAD VOX  \xc2\xb7  TAKE 3") }, status { juce::String::fromUTF8 ("DEMO  \xc2\xb7  THE WORDS ARRIVE WITH ARA") };
    WordsWindow();
    void paint (juce::Graphics&) override;
    // live: Duality is an ARA extension on a vocal event; the window shows that event's waveform, following the song
    bool live = false;
    WordsView view;
    void setView (WordsView v);
    // what the window asks of the words (select, correct, transcribe again, place the lyrics, move the song)
    std::function<void (const WordsAction&)> onAction;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override;
private:
    void paintLive (juce::Graphics&);
    // where things were drawn, for the mouse
    struct Hit { juce::Rectangle<float> box; int index; };   // index into view.words
    std::vector<Hit> hits;
    juce::Rectangle<float> transcribeKey, lyricsKey, wordRow, waveArea;
    double shownT0 = 0.0, shownSpan = 10.0; float shownX0 = 0, shownW = 1;
    double scrollT0 = -1.0;   // the song stopped: where the window was scrolled to (-1: following the song)
    double zoomSpan = 8.0;    // seconds across the window (Cmd/Ctrl + scroll, or pinch, zooms)
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    int anchor = -1;          // the word a drag started on
    bool dragged = false;
    int hitAt (juce::Point<float>, bool nearest) const;
    void selectRange (int a, int b);
    // correcting a word: a text field over it
    std::unique_ptr<juce::TextEditor> wordField;
    int editingId = -1;
    void beginEdit (int index);
    void endEdit (bool keep);
    // the lyrics as written: a page over the window
    std::unique_ptr<juce::TextEditor> lyricsPage;
    struct KeyLook : juce::LookAndFeel_V4   // (before the keys: it outlives them)
    {
        juce::Font getTextButtonFont (juce::TextButton&, int) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    } keyLook;
    std::unique_ptr<juce::TextButton> placeKey, cancelKey;
    void closeLyrics (bool place);
public:
    void openLyrics();
};

// Duality: the Apply plate, six keys that put an effect on the selected words
class ApplyKeys : public juce::Component, public juce::SettableTooltipClient
{
public:
    int onMask = 0b10011;   // the effects on the selected words (bit 0 Throw, 1 Echo, 2 Swell, 3 Double, 4 Harmony, 5 Shift): they stack
    int edit = 1;           // the effect whose settings the Words knobs show
    std::function<void (int)> onKey;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    static constexpr int panelW = 196, panelH = 172;
    juce::Rectangle<float> keyBounds (int k) const;
};

// Equanimity: which bank of settings is in use. L/R mode has one (0); M/S mode has Mid (1) and Side (2), and the panel
// edits one of them, or both together (link).
inline int eqBankOf (juce::AudioProcessorValueTreeState& a)
{
    auto raw = [&] (const char* id) { auto* v = a.getRawParameterValue (id); return v != nullptr ? v->load() : 0.0f; };
    return raw ("msmode") < 0.5f ? 0 : (juce::roundToInt (raw ("msedit")) == 2 ? 2 : 1);
}
inline bool eqLinked (juce::AudioProcessorValueTreeState& a)
{
    auto raw = [&] (const char* id) { auto* v = a.getRawParameterValue (id); return v != nullptr ? v->load() : 0.0f; };
    return raw ("msmode") > 0.5f && juce::roundToInt (raw ("msedit")) == 1;
}

// The Path panel: a brass plate with five keys, M = S and L/R M/S, in the manner of a mid/side EQ's mode buttons
class ModeButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Key { keyMid, keyLink, keySide, keyLR, keyMS };
    ModeButtons() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setState (int msMode, int edit, int dirtyMask)   // dirtyMask: bit 0 Stereo, 1 Mid, 2 Side bank has settings
    { if (msMode != mode || edit != editIdx || dirtyMask != dirty) { mode = msMode; editIdx = edit; dirty = dirtyMask; repaint(); } }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { if (pressed >= 0) { pressed = -1; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { if (pressed >= 0) { pressed = -1; repaint(); } }
    std::function<void (int)> onKey;
    static constexpr int panelW = 170, panelH = 204;
private:
    juce::Rectangle<float> keyBounds (int key) const;
    int mode = 0, editIdx = 0, dirty = 0, pressed = -1;
};

class ArtifactsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ArtifactsEditor (ArtifactsProcessor&);

    // The case around the plate: 0 = original cherry, 1 = oiled walnut, 2 = teak cheeks with steel rails,
    // 3 = tolex with piping and chrome corners, 4 = hammertone steel, 5 = glazed ceramic mended with gold
    static constexpr int defaultCaseStyle = 5;   // the glazed ceramic case, mended with gold
    static inline int caseStyleOverride = -1;   // for previews
    static inline int mockVariant = ARTIFACTS_VARIANT;   // 1 Kintsugi, 2 Console RA-1, 3 Duality, 4 Anemoia, 5 Equanimity (set per plugin at build time)
    bool sizeReady = false, laidOut = false;
    float pendingTextBoost = 1.0f; double resizeAt = 0;   // re-drawn with larger writing once resizing stops
    void applyTextBoost (float b);
    void layoutPanel();   // places every part at the design size (resized() then scales the lot)
    float uiScale = 1.0f;   // the window's size relative to the design size (the panel is drawn at the design size and scaled)
    static int designW();
    static int designH();
    ~ArtifactsEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    // The metal front plate; all controls live on it, the editor draws the wooden case around it
    struct PlateArea : public juce::Component
    {
        juce::Image* image = nullptr;
        void paint (juce::Graphics&) override;
    };
    PlateArea plateArea;
    void renderHousing();
    void renderHousingCherry();
    void renderHousingStyled (int style);
    void renderHousingCeramic();
    int caseStyle = defaultCaseStyle;
    juce::Image housing;

    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label readout;
        juce::String name;
        int steps = 0;                    // 0 = continuous, >1 = stepped switch
        bool alwaysShowValue = false;
        bool printed = true;
        std::vector<std::pair<float, juce::String>> marks;   // printed scale values
        juce::StringArray stepLabels;     // printed at the positions of a stepped switch
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    Knob& addKnob (const juce::String& paramId, const juce::String& name, int steps = 0);
    Knob& addSwitch (const juce::String& paramId, const juce::String& name, juce::StringArray labels, float spread);
    void placeKnob (Knob&, float col, int row, juce::Point<int> sectionOrigin, int colWidth = 0);
    void renderPlate();
    void toggleParam (const juce::String& slot);
    juce::String rid (const juce::String& slot) const;   // which parameter of this plugin a slot of the panel is bound to
    bool hasParam (const juce::String& slot) const;
    float paramValue (const juce::String& slot, float fallback) const;

    ArtifactsProcessor& proc;
    KintsugiLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *cDrive {}, *iron {}, *hum {}, *channel {}, *glue {}, *drift {};
    Knob *vDrive {}, *bias {}, *sag {}, *vType {}, *tone {}, *vMix {};
    Knob *speed {}, *tSat {}, *wow {}, *flutter {}, *wear {};
    Knob *mix {}, *output {};

    PilotLamp consoleLamp, valveLamp, tapeLamp, peakLamp;
    float peakHold = 0.0f;
    ToggleSwitch consoleSwitch, valveSwitch, tapeSwitch;
    PilotLamp noiseLamp;      // Equanimity: the hardware's noise floor in / out
    ToggleSwitch noiseSwitch;
    float valveGlow = 0.0f;
    juce::Random flicker;

    DrumWindow channelDrum { [] { juce::StringArray a; for (int i = 1; i <= 24; ++i) a.add (juce::String (i)); return a; }() };
    DrumWindow xfmrDrum { { "NICKEL", "STEEL", "IRON" } };
    DrumWindow speedDrum { { juce::String::fromUTF8 ("7\xc2\xbd"), "15", "30" } };

    IRWindow irWindow;
    WordsWindow wordsWindow;
    bool throwPage = false;   // Duality mock-up: which Words page is shown
    int wordsTick = 0;
    bool previewLive = false;
    ApplyKeys applyKeys;
    DrumWindow dTime { juce::StringArray { "375 MS" } }, dDiv { juce::StringArray { "1/8" } }, dFeel { juce::StringArray { "DOTTED" } },
               dPat { juce::StringArray { "SCATTER" } }, dPitch { juce::StringArray { "+7 ST" } };
    ToggleSwitch meterSwitch;
    std::shared_ptr<juce::FileChooser> irChooser;
    void chooseIR();
    VUMeter vuL { "L", 0 }, vuR { "R", 0 };   // a matched pair (variant 1, a near-twin from another maker, is kept in the code)

    struct Section { juce::String title; juce::Rectangle<int> bounds; PilotLamp* lamp = nullptr; ToggleSwitch* toggle = nullptr; };
    std::vector<Section> sections;

    juce::Image plate;

    // Equanimity: the Path panel and which bank of settings the knobs are bound to
    ModeButtons modeButtons;
    bool eqLinkShown = false;
    int eqBank = 0;                 // 0 L/R, 1 Mid, 2 Side
    void rebindBank();
    bool bankDirty (int bank) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArtifactsEditor)
};
