// Deck RA-1: renders the iPhone panel with the Artifacts suite's own drawing code.
//
// It compiles the suite editor (UI/PluginEditor.cpp) into this program, so the case, the plate, the print and its wear,
// the screws, the brackets, the lamps, the keys and the drum windows are the very same code as on Console RA-1,
// Kintsugi, Duality, Anemoia and Equanimity. It writes:
//   mock      <dir>     the two screens as PNG mock-ups (with the iPhone's Dynamic Island and home bar drawn in)
//   assets    <dir>     what the web app is built from: backgrounds, key and lamp sprites, drum strips, layout.json, icon
#define ARTIFACTS_VARIANT 2
#include "../../../UI/PluginEditor.cpp"

using namespace juce;

namespace Deck
{
// ------------------------------------------------------------------ what is on the panel
struct KeyDef { String id; int cc; String label; String icon; Rectangle<float> r; bool big = false; bool cream = false; };   // cream: the colour of the suite's cream knobs
struct LampDef { String id; int cc; String colour; Point<float> c; };   // colour: green, red, amber, link
struct Bracket { String title; Rectangle<int> r; };
struct Layout
{
    String name;
    int screenW, screenH;
    Rectangle<float> plate;              // in screen points
    std::vector<KeyDef> keys;            // plate coordinates
    std::vector<LampDef> lamps;
    std::vector<Bracket> brackets;
    Rectangle<float> counter;            // the leatherette frame
    float titleSize = 32.0f;
    Point<float> title;
    Rectangle<float> maker;              // RELUCTANT / ASTRONAUT
    float grooveY = 0;
    bool fullScreen = false;           // the plate is the whole screen, no case around it
    float cornerR = 55.0f;             // the display's own corner radius (iPhone 15 about 55; the iPad 7 has square corners)
    float pxScale = 3.0f;              // pixels per point on the device
    float linkScale = 1.0f;            // the LINK lamp's size (the iPad's is larger)
    Point<float> linkLabel;
    std::vector<Rectangle<float>> drums; // drum windows (plate coordinates), filled in by the counter painter
    std::vector<std::pair<String, Rectangle<float>>> drumLabels;
    std::vector<Point<float>> drumDots;
};

// The keys, by name: cc, label, icon
struct KeyInfo { int cc; const char* label; const char* icon; };
static KeyInfo info (const String& id)
{
    static const std::map<String, KeyInfo> m {
        { "rew", { 20, "REW", "rew" } }, { "ffw", { 21, "FWD", "ffw" } }, { "stop", { 22, "STOP", "stop" } },
        { "play", { 23, "PLAY", "play" } }, { "rec", { 24, "RECORD", "rec" } }, { "cycle", { 25, "CYCLE", "cycle" } },
        { "click", { 26, "CLICK", "click" } }, { "start", { 30, "START", "zero" } }, { "prev", { 31, "PREV", "prev" } },
        { "next", { 32, "NEXT", "next" } }, { "add", { 33, "ADD", "addm" } }, { "mixer", { 40, "MIXER", "mixer" } },
        { "mix2", { 41, "MIX 2", "" } }, { "editor", { 43, "EDITOR", "" } }, { "left", { 44, "LEFT", "" } },
        { "lower", { 45, "LOWER", "" } }, { "right", { 46, "RIGHT", "" } }, { "vsti", { 47, "VST INSTR", "" } },
        { "undo", { 50, "UNDO", "" } }, { "redo", { 51, "REDO", "" } }, { "save", { 52, "SAVE", "" } },
        { "split", { 53, "SPLIT", "" } }, { "user", { 60, "USER", "" } },
        { "mix3", { 42, "MIX 3", "" } }, { "dup", { 54, "DUPLICATE", "" } }, { "zfull", { 55, "ZOOM FULL", "" } },
        { "zin", { 56, "ZOOM IN", "" } }, { "zout", { 57, "ZOOM OUT", "" } }, { "punchin", { 58, "PUNCH IN", "" } },
        { "punchout", { 59, "PUNCH OUT", "" } },
        { "channel", { 41, "CHANNEL", "" } }, { "markers", { 43, "MARKERS", "" } }, { "pool", { 43, "POOL", "" } }, { "audioconn", { 42, "CONNECTIONS", "" } },
        { "trkup", { 61, "UP", "up" } }, { "trkdown", { 62, "DOWN", "down" } },
        { "newver", { 53, "NEW VERSION", "" } }, { "arm", { 58, "ARM", "" } }, { "monitor", { 59, "MONITOR", "" } } };
    return m.at (id);
}

static void addRow (Layout& L, StringArray ids, float x, float y, float w, float h, float gap, bool big = false)
{
    const int n = ids.size();
    const float kw = (w - gap * (float) (n - 1)) / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const auto ki = info (ids[i]);
        L.keys.push_back ({ ids[i], ki.cc, ki.label, ki.icon, { x + (float) i * (kw + gap), y, kw, h }, big });
    }
}
static void lampAbove (Layout& L, const String& keyId, const String& colour, float y)
{
    for (auto& k : L.keys)
        if (k.id == keyId) L.lamps.push_back ({ keyId, k.cc, colour, { k.r.getCentreX(), y } });
}

// iPhone 15: 393 x 852 points; Dynamic Island and status bar in the top 59, home bar in the bottom 34
static Layout portrait()
{
    Layout L; L.name = "portrait"; L.screenW = 393; L.screenH = 852;
    L.plate = { 12.0f, 60.0f, 369.0f, 762.0f };
    const float W = L.plate.getWidth(), x0 = 18.0f, w = W - 36.0f;
    L.title = { 20.0f, 14.0f }; L.titleSize = 31.0f;
    L.lamps.push_back ({ "link", -1, "link", { W - 50.0f, 30.0f } });
    L.linkLabel = { W - 50.0f, 48.0f };
    L.grooveY = 70.0f;
    L.brackets.push_back ({ "Counter", Rectangle<int> (10, 92, (int) W - 20, 10) });
    L.counter = { 18.0f, 104.0f, w, 86.0f };
    float y = 222.0f;
    L.brackets.push_back ({ "Transport", Rectangle<int> (10, (int) y, (int) W - 20, 10) });
    addRow (L, { "stop", "play", "rec" }, x0, y + 34.0f, w, 74.0f, 12.0f, true);
    lampAbove (L, "play", "green", y + 21.0f); lampAbove (L, "rec", "red", y + 21.0f);
    addRow (L, { "rew", "ffw", "cycle", "click" }, x0, y + 136.0f, w, 48.0f, 10.0f);
    lampAbove (L, "cycle", "amber", y + 123.0f); lampAbove (L, "click", "amber", y + 123.0f);
    y = 432.0f;
    L.brackets.push_back ({ "Locate", Rectangle<int> (10, (int) y, (int) W - 20, 10) });
    addRow (L, { "start", "prev", "next", "add" }, x0, y + 16.0f, w, 46.0f, 10.0f);
    y = 522.0f;
    L.brackets.push_back ({ "Windows", Rectangle<int> (10, (int) y, (int) W - 20, 10) });
    addRow (L, { "mixer", "mix2", "editor", "vsti" }, x0, y + 16.0f, w, 44.0f, 10.0f);
    addRow (L, { "left", "lower", "right", "user" }, x0, y + 68.0f, w, 44.0f, 10.0f);
    y = 662.0f;
    L.brackets.push_back ({ "Edit", Rectangle<int> (10, (int) y, (int) W - 20, 10) });
    addRow (L, { "undo", "redo", "save", "split" }, x0, y + 16.0f, w, 44.0f, 10.0f);
    L.maker = { 0.0f, 727.0f, W, 30.0f };
    return L;
}

// on its side: 852 x 393; the island on one end, so 59 kept clear left and right; home bar 21 at the bottom
static Layout landscape()
{
    // transport only, full screen: the plate in a thin hammertone edge; the keys clear of the island (59 each end) and the home bar
    Layout L; L.name = "landscape"; L.screenW = 852; L.screenH = 393;
    L.plate = { 8.0f, 8.0f, 836.0f, 377.0f };
    L.fullScreen = true;
    const float W = L.plate.getWidth();
    L.title = { 76.0f, 18.0f }; L.titleSize = 27.0f;
    L.lamps.push_back ({ "link", -1, "link", { W - 96.0f, 30.0f } });
    L.linkLabel = { W - 96.0f, 47.0f };
    L.grooveY = 80.0f;
    const float lx = 68.0f, lw = W - 136.0f;
    L.brackets.push_back ({ "Transport", Rectangle<int> ((int) lx - 8, 102, (int) lw + 16, 10) });
    addRow (L, { "rew", "ffw", "stop", "play", "rec", "mixer" }, lx, 136.0f, lw, 158.0f, 12.0f, true);
    lampAbove (L, "play", "green", 123.0f); lampAbove (L, "rec", "red", 123.0f);
    L.maker = { lx, 316.0f, lw, 30.0f };
    return L;
}

// iPad (7th generation): 1080 x 810 points at 2x, square display corners, a 20-point status bar on top
static Layout ipad()
{
    Layout L; L.name = "ipad"; L.screenW = 1080; L.screenH = 810;
    L.plate = { 10.0f, 10.0f, 1060.0f, 790.0f };
    L.fullScreen = true; L.cornerR = 0.0f; L.pxScale = 2.0f;
    const float W = L.plate.getWidth();
    L.title = { 58.0f, 44.0f }; L.titleSize = 42.0f;
    L.lamps.push_back ({ "link", -1, "link", { W - 92.0f, 62.0f } });
    L.linkLabel = { W - 92.0f, 79.0f };
    L.grooveY = 132.0f;
    const float lx = 60.0f, lw = W - 120.0f;
    L.brackets.push_back ({ "Transport", Rectangle<int> ((int) lx - 10, 180, (int) lw + 20, 10) });
    addRow (L, { "rew", "ffw", "stop", "play", "rec", "mixer" }, lx, 230.0f, lw, 380.0f, 20.0f, true);
    lampAbove (L, "play", "green", 210.0f); lampAbove (L, "rec", "red", 210.0f);
    L.maker = { lx, 680.0f, lw, 30.0f };
    return L;
}

// iPad, the full studio deck: transport and modes, locate and edit, windows, arrange. Every key the Cubase script maps.
static Layout ipadFull()
{
    Layout L; L.name = "ipad"; L.screenW = 1080; L.screenH = 810;
    L.plate = { 10.0f, 10.0f, 1060.0f, 790.0f };
    L.fullScreen = true; L.cornerR = 0.0f; L.pxScale = 2.0f;
    const float W = L.plate.getWidth();
    L.title = { 58.0f, 30.0f }; L.titleSize = 40.0f;
    L.lamps.push_back ({ "link", -1, "link", { W - 92.0f, 50.0f } });
    L.linkScale = 1.6f;
    L.linkLabel = { W - 92.0f, 77.0f };
    L.grooveY = 108.0f;
    const float x0 = 50.0f, full = W - 100.0f, gap = 40.0f;
    auto bracket = [&] (const char* t, float x, float w, float y) { L.brackets.push_back ({ t, Rectangle<int> ((int) x - 8, (int) y, (int) w + 16, 10) }); };
    // transport and modes
    const float tw = 610.0f, mw = full - tw - gap;
    bracket ("Transport", x0, tw, 142.0f); bracket ("Mode & Mix", x0 + tw + gap, mw, 142.0f);
    addRow (L, { "start", "rew", "ffw", "stop", "play", "rec" }, x0, 176.0f, tw, 140.0f, 14.0f, true);
    addRow (L, { "cycle", "mixer" }, x0 + tw + gap, 176.0f, mw, 140.0f, 14.0f, true);
    lampAbove (L, "play", "green", 163.0f); lampAbove (L, "rec", "red", 163.0f);
    lampAbove (L, "cycle", "amber", 163.0f);
    // track and arrange, right under the transport, with larger keys
    bracket ("Track & Arrange", x0, tw, 344.0f); bracket ("Track Select", x0 + tw + gap, mw, 344.0f);
    addRow (L, { "zfull", "zin", "zout", "arm", "monitor", "channel" }, x0, 366.0f, tw, 86.0f, 12.0f, false);
    addRow (L, { "trkup", "trkdown" }, x0 + tw + gap, 366.0f, mw, 86.0f, 14.0f, true);   // DOWN sits under MIXER
    // locate and edit
    const float lw = 530.0f, ew = full - lw - gap;
    bracket ("Locate", x0, lw, 482.0f); bracket ("Edit", x0 + lw + gap, ew, 482.0f);
    addRow (L, { "prev", "next", "add", "click" }, x0, 504.0f, lw, 80.0f, 12.0f, true);
    addRow (L, { "undo", "redo", "save", "newver" }, x0 + lw + gap, 504.0f, ew, 80.0f, 12.0f, false);
    // windows
    bracket ("Windows", x0, full, 614.0f);
    addRow (L, { "dup", "audioconn", "vsti", "left", "lower", "right" }, x0, 636.0f, full, 60.0f, 12.0f, false);
    for (auto& k : L.keys)   // the Windows row in cream
        for (auto id : { "dup", "audioconn", "vsti", "left", "lower", "right" }) if (k.id == id) k.cream = true;
    L.maker = { x0, 722.0f, full, 30.0f };
    return L;
}

// ------------------------------------------------------------------ icons (the prototype's, as paths)
static Path icon (const String& name)
{
    auto p = [] (const char* d) { return Drawable::parseSVGPath (d); };
    Path r;
    if (name == "rew")   { r = p ("M22 0 L0 13 L22 26Z M44 0 L22 13 L44 26Z"); }
    else if (name == "ffw")   { r = p ("M0 0 L22 13 L0 26Z M22 0 L44 13 L22 26Z"); }
    else if (name == "stop")  { r.addRoundedRectangle (0.0f, 0.0f, 26.0f, 26.0f, 2.0f); }
    else if (name == "play")  { r = p ("M0 0 L28 15 L0 30Z"); }
    else if (name == "rec")   { r.addEllipse (0.0f, 0.0f, 30.0f, 30.0f); }
    else if (name == "cycle") { r = p ("M8 4 H28 A10 10 0 0 1 28 24 H22 V19 H28 A5 5 0 0 0 28 9 H8 V14 L0 6.5 L8 -1Z M30 24 H10 A10 10 0 0 1 10 4 H16 V9 H10 A5 5 0 0 0 10 19 H30 V14 L38 21.5 L30 29Z"); }
    else if (name == "click") { r = p ("M12 6 L14 6 L20 26 L18 27Z"); r.addRectangle (0.0f, 28.0f, 26.0f, 4.0f);
                                Path body = p ("M9 0 H17 L26 32 H0Z"); PathStrokeType (1.6f).createStrokedPath (body, body); r.addPath (body); }
    else if (name == "mixer") { for (float x : { 5.0f, 19.0f, 33.0f }) r.addRectangle (x, 0.0f, 2.0f, 34.0f);
                                r.addRoundedRectangle (0.0f, 20.0f, 12.0f, 7.0f, 1.0f); r.addRoundedRectangle (14.0f, 6.0f, 12.0f, 7.0f, 1.0f); r.addRoundedRectangle (28.0f, 15.0f, 12.0f, 7.0f, 1.0f); }
    else if (name == "up")    { r = p ("M15 0 L30 22 L0 22Z"); }
    else if (name == "down")  { r = p ("M0 0 L30 0 L15 22Z"); }
    else if (name == "zero")  { r = p ("M30 0 L6 12 L30 24Z"); r.addRectangle (0.0f, 0.0f, 4.0f, 24.0f); }
    else if (name == "prev")  { r = p ("M14 0 L0 12 L14 24Z M18 2 H30 L34 8 L30 14 H18Z"); }
    else if (name == "next")  { r = p ("M0 2 H12 L16 8 L12 14 H0Z M20 0 L34 12 L20 24Z"); }
    else if (name == "addm")  { r = p ("M0 2 H18 L24 10 L18 18 H0Z"); r.addRectangle (25.0f, 14.0f, 3.0f, 12.0f); r.addRectangle (20.5f, 18.5f, 12.0f, 3.0f); }
    r.setUsingNonZeroWinding (true);
    return r;
}

// ------------------------------------------------------------------ the parts, painted as the suite paints them
// A key: as the Path keys on Equanimity, larger. A dark oxblood cap with cream lettering in its well;
// held down it sits lower and lights amber from within.
static void paintKey (Graphics& g, const KeyDef& k, bool lit)
{
    const auto r = k.r;
    const float m = jmin (r.getWidth(), r.getHeight());
    const float rad = k.big ? 6.0f : 4.5f;
    const auto cap = r.reduced (1.0f).translated (lit ? 0.0f : -0.5f, lit ? 1.6f : 0.0f);
    if (! lit) { g.setColour (Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (cap.translated (1.2f, 2.0f), rad); }
    const Colour dk = k.cream ? Colour (0xffb3a27b) : Colour (0xff2a0705), lt = k.cream ? Colour (0xfff6efd8) : Colour (0xff8f3126);
    const float glow = lit ? 1.0f : 0.0f;
    if (glow > 0.0f && (cap.getHeight() > 250.0f || cap.getWidth() > cap.getHeight() * 1.4f))
    {
        // a tall key (iPad): the light spills evenly round its edges
        for (int i = 12; i >= 1; --i)
        {
            g.setColour (Colour (0xffffa040).withAlpha (0.045f * glow));
            g.fillRoundedRectangle (cap.expanded ((float) i * 2.6f), rad + (float) i * 2.6f);
        }
    }
    else if (glow > 0.0f)
    {
        const auto c = cap.getCentre();
        g.setGradientFill (ColourGradient (Colour (0xffffa040).withAlpha (0.62f * glow), c.x, c.y, Colour (0xffffa040).withAlpha (0.0f), c.x + jmax (r.getWidth(), r.getHeight()) * 0.75f, c.y, true));
        g.fillEllipse (cap.expanded (m * 0.55f, m * 0.55f));
    }
    g.setGradientFill (ColourGradient (lt.interpolatedWith (Colour (0xffffb257), glow * 0.9f), cap.getX(), cap.getY(),
                                       dk.interpolatedWith (Colour (0xffc4561a), glow * 0.9f), cap.getRight(), cap.getBottom(), false));
    g.fillRoundedRectangle (cap, rad);
    g.setColour (Colours::white.withAlpha (0.20f * (1.0f - 0.5f * glow)));     // gloss on the upper edge
    g.fillRoundedRectangle (Rectangle<float> (cap.getWidth() - 8.0f, 3.0f).withPosition (cap.getX() + 4.0f, cap.getY() + 2.0f), 1.5f);
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (cap, rad, 0.9f);

    const Colour ink = glow > 0.6f ? Colour (0xff2a1208) : k.cream ? Colour (0xff2a2218) : Colour (0xffeae1c4).withAlpha (0.92f);
    auto text = [&] (const String& s, Rectangle<float> box, float size, float kern)
    {
        g.setFont (KintsugiFonts::serif (size).withExtraKerningFactor (kern));
        g.setColour (k.cream ? Colours::white.withAlpha (0.45f) : Colours::black.withAlpha (0.4f));
        g.drawFittedText (s, box.translated (0.5f, 0.7f).toNearestInt(), Justification::centred, 1, 0.8f);
        g.setColour (ink);
        g.drawFittedText (s, box.toNearestInt(), Justification::centred, 1, 0.8f);
    };
    if (k.icon.isNotEmpty())
    {
        auto ic = icon (k.icon);
        const bool small = ! k.big;
        const float labelH = small ? 0.0f : (cap.getHeight() > 250.0f ? 40.0f : cap.getHeight() > 120.0f ? 26.0f : 15.0f);
        const float ih = small ? jmin (16.0f, cap.getHeight() * 0.36f) : jmin (cap.getHeight() > 250.0f ? 64.0f : cap.getHeight() > 120.0f ? 46.0f : 26.0f, cap.getHeight() * 0.36f);
        const auto ib = ic.getBounds();
        const float sc = ih / jmax (ib.getHeight(), ib.getWidth() * 0.62f);
        const float iconCy = cap.getCentreY() - labelH * 0.5f;
        ic.applyTransform (AffineTransform::translation (-ib.getCentreX(), -ib.getCentreY()).scaled (sc).translated (cap.getCentreX(), iconCy));
        const Colour iconInk = k.icon == "rec" && glow < 0.5f ? Colour (0xffe8573f) : ink;   // the record dot is red, as on every tape machine
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillPath (ic, AffineTransform::translation (0.5f, 0.8f));
        g.setColour (iconInk);
        g.fillPath (ic);
        if (! small)
            if (cap.getHeight() > 250.0f) text (k.label, { cap.getX(), cap.getBottom() - 58.0f, cap.getWidth(), 26.0f }, 19.0f, 0.22f);
            else if (cap.getHeight() > 120.0f) text (k.label, { cap.getX(), cap.getBottom() - 36.0f, cap.getWidth(), 20.0f }, 15.0f, 0.2f);
            else text (k.label, { cap.getX(), cap.getBottom() - 21.0f, cap.getWidth(), 14.0f }, 10.5f, 0.16f);
    }
    else
    {
        const bool tall = ! k.big && cap.getHeight() > 75.0f && cap.getWidth() > 110.0f;   // the iPad's Track & Arrange keys
        const float size = k.big ? 20.0f : tall ? (k.label.length() > 6 ? 13.5f : 15.5f) : (k.label.length() > 6 ? 11.5f : 13.0f);
        text (k.label, cap.reduced (3.0f, 0.0f), size, k.big ? 0.22f : 0.08f);
    }
}

// The jewel pilot lamp of the suite (knurled chrome bezel, faceted lens), in amber, red or green
static void paintLamp (Graphics& g, Point<float> c, const String& colour, float lit)
{
    const float rb = 9.5f, rl = 6.4f;
    struct Tint { Colour glow, darkOff, darkOn, lightOff, lightOn; };
    const Tint amber { Colour (0xffffa040), Colour (0xff3a160a), Colour (0xffd2560f), Colour (0xff7a3418), Colour (0xffffd27a) };
    const Tint red   { Colour (0xffff3020), Colour (0xff3a0b0b), Colour (0xffd01a14), Colour (0xff6e1a1a), Colour (0xffff8a70) };
    const Tint green { Colour (0xff50ff70), Colour (0xff0b2a12), Colour (0xff1c9a35), Colour (0xff1f5228), Colour (0xffb4ffbe) };
    const Tint& t = colour == "red" ? red : colour == "green" ? green : amber;
    if (lit > 0.01f)
    {
        g.setGradientFill (ColourGradient (t.glow.withAlpha (0.42f * lit), c.x, c.y, t.glow.withAlpha (0.0f), c.x + rb * 2.2f, c.y, true));
        g.fillEllipse (Rectangle<float> (rb * 4.4f, rb * 4.4f).withCentre (c));
    }
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillEllipse (Rectangle<float> (rb * 2 + 2, rb * 2 + 2).withCentre (c.translated (0.8f, 1.4f)));
    g.setGradientFill (ColourGradient (Colour (0xfff2f2ee), c.x - rb, c.y - rb, Colour (0xff4a4c49), c.x + rb, c.y + rb, false));
    g.fillEllipse (Rectangle<float> (rb * 2, rb * 2).withCentre (c));
    for (int i = 0; i < 40; ++i)
    {
        const float a = (float) i * MathConstants<float>::twoPi / 40.0f;
        g.setColour (Colours::black.withAlpha (0.2f));
        g.drawLine ({ c.getPointOnCircumference (rb * 0.82f, a), c.getPointOnCircumference (rb, a) }, 0.6f);
    }
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawEllipse (Rectangle<float> (rl * 2 + 1.2f, rl * 2 + 1.2f).withCentre (c), 1.0f);
    const Colour lensDark = t.darkOff.interpolatedWith (t.darkOn, lit), lensLight = t.lightOff.interpolatedWith (t.lightOn, lit);
    g.setGradientFill (ColourGradient (lensLight, c.x - 1.5f, c.y - 1.5f, lensDark, c.x + rl, c.y + rl, true));
    g.fillEllipse (Rectangle<float> (rl * 2, rl * 2).withCentre (c));
    for (int i = 0; i < 8; ++i)
    {
        const float a0 = (float) i * MathConstants<float>::twoPi / 8.0f, a1 = a0 + MathConstants<float>::twoPi / 8.0f;
        Path f;
        f.startNewSubPath (c.getPointOnCircumference (rl * 0.45f, (a0 + a1) * 0.5f));
        f.lineTo (c.getPointOnCircumference (rl, a0));
        f.lineTo (c.getPointOnCircumference (rl, a1));
        f.closeSubPath();
        g.setColour ((i % 2 ? Colours::white : Colours::black).withAlpha (0.14f - 0.04f * lit));
        g.fillPath (f);
    }
    g.setColour (Colours::white.withAlpha (0.4f + 0.35f * lit));
    g.fillEllipse (Rectangle<float> (2.6f, 2.0f).withCentre (c.translated (-2.2f, -2.4f)));
}

// ---- the tape counter: the Console's leatherette meter frame, a cream meter card, and drum windows cut into it
// digits: bars (4) . beats (1) . sixteenths (1) . ticks (3)
static void layoutCounter (Layout& L)
{
    if (L.counter.isEmpty()) return;
    // a running time: hours : minutes : seconds
    const auto face = L.counter.reduced (7.0f).reduced (3.4f);
    const float labelH = 9.0f;
    const float dh = jmin (44.0f, (face.getHeight() - 8.0f - labelH) * 0.92f), dw = dh * 0.66f;
    const int groups[3] = { 2, 2, 2 };
    const char* names[3] = { "HRS", "MIN", "SEC" };
    const float inGap = 3.0f, grpGap = dw * 0.62f;
    float total = 0;
    for (int gi = 0; gi < 3; ++gi) total += (float) groups[gi] * dw + (float) (groups[gi] - 1) * inGap;
    total += 2.0f * grpGap;
    float x = face.getCentreX() - total * 0.5f;
    const float y = face.getY() + 9.5f;
    L.drums.clear(); L.drumLabels.clear(); L.drumDots.clear();
    for (int gi = 0; gi < 3; ++gi)
    {
        const float gx = x;
        for (int d = 0; d < groups[gi]; ++d) { L.drums.push_back ({ x, y, dw, dh }); x += dw + (d < groups[gi] - 1 ? inGap : 0.0f); }
        L.drumLabels.push_back ({ names[gi], { gx - 10.0f, y + dh + 1.5f, x - gx + 20.0f, labelH } });
        if (gi < 2)
        {
            L.drumDots.push_back ({ x + grpGap * 0.5f, y + dh * 0.33f });   // a colon between the groups
            L.drumDots.push_back ({ x + grpGap * 0.5f, y + dh * 0.67f });
            x += grpGap;
        }
    }
}

static void paintCounterBody (Graphics& g, const Layout& L)
{
    if (L.counter.isEmpty()) return;
    const auto vb = L.counter;
    // the leatherette frame (Console RA-1's meter frame)
    g.setColour (Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (vb.translated (1.2f, 2.0f), 3.5f);
    g.setGradientFill (ColourGradient (Colour (0xff35403c), vb.getX(), vb.getY(), Colour (0xff1f2724), vb.getRight(), vb.getBottom(), false));
    g.fillRoundedRectangle (vb, 3.5f);
    {
        Graphics::ScopedSaveState ss (g);
        Path cp; cp.addRoundedRectangle (vb, 3.5f);
        g.reduceClipRegion (cp);
        Random rng (88);
        for (int i = 0; i < (int) (vb.getWidth() * vb.getHeight() * 0.55f); ++i)
        {
            const Point<float> o (vb.getX() + rng.nextFloat() * vb.getWidth(), vb.getY() + rng.nextFloat() * vb.getHeight());
            const float d = 0.6f + 1.0f * rng.nextFloat();
            g.setColour ((rng.nextInt (3) == 0 ? Colour (0xff0c110f) : Colour (0xffa9b8ae)).withAlpha (0.07f + 0.15f * rng.nextFloat()));
            g.fillEllipse (Rectangle<float> (d * (1.0f + rng.nextFloat()), d).withCentre (o));
        }
    }
    g.setColour (Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (vb.reduced (0.6f).translated (0.3f, 0.4f), 3.2f, 0.7f);
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (vb, 3.5f, 0.9f);

    // the cream card, as the edgewise meters: thin bezel, dark border, papery face, the lamp strip along the top
    const auto b = vb.reduced (7.0f), f = b.reduced (2.6f);
    g.setGradientFill (ColourGradient (Colour (0xfffaf3dc), b.getX(), b.getY(), Colour (0xffc9bd98), b.getRight(), b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.6f);
    g.setColour (Colour (0xff4a3f28).withAlpha (0.8f));
    g.drawRoundedRectangle (b, 3.6f, 0.7f);
    g.setColour (Colour (0xff1a1710));
    g.fillRoundedRectangle (f.expanded (0.7f), 2.4f);
    g.setGradientFill (ColourGradient (Colour (0xfff6edc8), f.getCentreX(), f.getBottom(), Colour (0xffe6d8a6), f.getX(), f.getY(), true));
    g.fillRoundedRectangle (f, 2.0f);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (f.toNearestInt());
        Random rng (4711);
        for (int i = 0; i < (int) (f.getWidth() * f.getHeight() * 0.4f); ++i)   // paper grain
        {
            const Point<float> q (f.getX() + rng.nextFloat() * f.getWidth(), f.getY() + rng.nextFloat() * f.getHeight());
            g.setColour ((rng.nextBool() ? Colours::white : Colour (0xff6b5228)).withAlpha (0.04f + 0.04f * rng.nextFloat()));
            g.fillRect (Rectangle<float> (0.5f, 0.5f).withCentre (q));
        }
        const float fh = f.getHeight(), fw = f.getWidth();
        g.setGradientFill (ColourGradient (Colour (0xff2e241b), 0.0f, f.getY(), Colour (0xff5a4a3a), 0.0f, f.getY() + 7.0f, false));
        g.fillRect (f.getX(), f.getY(), fw, 6.0f);
        g.setColour (Colour (0xffa59a88).withAlpha (0.55f));
        for (auto [px, pw] : { std::pair { 0.30f, 0.14f }, std::pair { 0.56f, 0.20f }, std::pair { 0.86f, 0.10f } })
            g.fillRect (f.getX() + fw * px, f.getY() + 2.5f, fw * pw, 1.0f);
        (void) fh;
        const Colour ink (0xff1d1a16);
        // printed on the card: the group names, the dots between them, the maker in the corner
        g.setColour (ink);
        g.setFont (KintsugiFonts::serif (7.5f, true).withExtraKerningFactor (0.22f));
        for (auto& [t, r] : L.drumLabels) g.drawText (t, r, Justification::centred);
        for (auto d : L.drumDots) g.fillEllipse (Rectangle<float> (3.4f, 3.4f).withCentre (d));
    }
    // the drum windows: the cut edge of the card, dark on the upper lip, catching light below
    for (auto ap : L.drums)
    {
        g.setColour (Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (ap.expanded (1.6f), 2.4f);
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (ap.expanded (1.8f).translated (0.5f, 0.6f), 2.4f, 0.6f);
        g.setColour (Colour (0xffe9dcb8));
        g.fillRect (ap);
    }
}

// one drum, turned to 'pos' (0 = blank, 1..10 = digits 0..9), as DrumWindow paints it
static const char* drumItems[] = { " ", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
static void paintDrumFace (Graphics& g, Rectangle<float> ap, float pos)
{
    const float itemH = ap.getHeight() * 0.95f;
    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (ap.toNearestInt());
    g.setColour (Colour (0xffe9dcb8));
    g.fillRect (ap);
    const int centre = (int) std::floor (pos + 0.5f);
    for (int k = centre - 1; k <= centre + 1; ++k)
    {
        if (k < 0 || k > 10) continue;
        const float y = ap.getCentreY() + ((float) k - pos) * itemH;
        g.setColour (Colour (0xff1c1812));
        g.setFont (KintsugiFonts::serif (ap.getHeight() * 0.78f, true));
        g.drawText (drumItems[k], Rectangle<float> (ap.getX(), y - itemH * 0.5f, ap.getWidth(), itemH), Justification::centred);
    }
}
static void paintDrumShade (Graphics& g, Rectangle<float> ap)
{
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.55f), ap.getX(), ap.getY(),
                                       Colours::black.withAlpha (0.0f), ap.getX(), ap.getY() + ap.getHeight() * 0.42f, false));
    g.fillRect (ap);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.0f), ap.getX(), ap.getBottom() - ap.getHeight() * 0.42f,
                                       Colours::black.withAlpha (0.5f), ap.getX(), ap.getBottom(), false));
    g.fillRect (ap);
    g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.45f), ap.getX(), ap.getY(),
                                       Colours::black.withAlpha (0.0f), ap.getX() + 3.0f, ap.getY() + 3.0f, false));
    g.fillRect (ap);
}


// iPhone 15: the display's own corners are rounded (about 55 points), so the hammertone edge follows them.
// The edge is the suite's hammertone, computed the same way, with the frame's profile taken across the curved edge.
static float roundedInset (float px, float py, float w, float h, float r)   // distance in from the edge of a rounded rectangle
{
    const float qx = std::abs (px - w * 0.5f) - (w * 0.5f - r), qy = std::abs (py - h * 0.5f) - (h * 0.5f - r);
    const float outside = std::sqrt (jmax (qx, 0.0f) * jmax (qx, 0.0f) + jmax (qy, 0.0f) * jmax (qy, 0.0f)) + jmin (jmax (qx, qy), 0.0f) - r;
    return -outside;
}
static Image roundedEdge (const Layout& L, const Image& plate, float scale)
{
    const int iw = (int) (L.screenW * scale), ih = (int) (L.screenH * scale);
    const float edge = L.plate.getX() * scale;            // frame width, pixels
    const float R = L.cornerR * scale;
    Image screen (Image::RGB, iw, ih, true);
    const float Lx = -0.48f, Ly = -0.56f, Lz = 0.68f;
    const float hx = Lx, hy = Ly, hz = Lz + 1.0f, hl = std::sqrt (hx * hx + hy * hy + hz * hz);
    {
        Image::BitmapData bd (screen, Image::BitmapData::writeOnly);
        for (int y = 0; y < ih; ++y)
            for (int x = 0; x < iw; ++x)
            {
                const float fx = (float) x + 0.5f, fy = (float) y + 0.5f;
                const float d = roundedInset (fx, fy, (float) iw, (float) ih, R);
                if (d < 0.0f || d > edge + 2.0f) continue;   // outside the glass, or under the plate
                // inward direction of the edge here (the gradient of the distance)
                const float gx = roundedInset (fx + 1.0f, fy, (float) iw, (float) ih, R) - roundedInset (fx - 1.0f, fy, (float) iw, (float) ih, R);
                const float gy = roundedInset (fx, fy + 1.0f, (float) iw, (float) ih, R) - roundedInset (fx, fy - 1.0f, (float) iw, (float) ih, R);
                const float gl = jmax (1.0e-6f, std::sqrt (gx * gx + gy * gy));
                const float a = jlimit (0.0f, 1.0f, d / edge);
                const float da = 0.004f;
                const float slope = (caseProfile (4, false, jmin (1.0f, a + da)) - caseProfile (4, false, jmax (0.0f, a - da))) / (2.0f * da);
                float nx = -slope * gx / gl, ny = -slope * gy / gl;
                // hammertone: shallow hammered dimples under a metallic grey-green paint
                const float f = 0.075f;
                const float hC = worley (fx * f, fy * f, 81), hX = worley ((fx + 1.0f) * f, fy * f, 81), hY = worley (fx * f, (fy + 1.0f) * f, 81);
                nx -= jlimit (-0.5f, 0.5f, (hX - hC) * 1.2f); ny -= jlimit (-0.5f, 0.5f, (hY - hC) * 1.2f);
                const float flake = valueNoise (fx * 1.7f, fy * 1.7f, 82);
                Colour base = Colour (0xff2c3532).interpolatedWith (Colour (0xff3e4945), flake);
                const float specStrength = 0.24f + 0.12f * (flake > 0.85f ? 1.0f : 0.0f), shininess = 22.0f;
                if (a < 0.05f) base = base.interpolatedWith (Colour (0xff9aa09c), 0.35f * (1.0f - a / 0.05f));
                const float nl = std::sqrt (nx * nx + ny * ny + 1.0f);
                const float ndl = jmax (0.0f, (nx * Lx + ny * Ly + Lz) / nl);
                const float shade = 0.42f + 0.58f * ndl / Lz;
                const float ndh = jmax (0.0f, (nx * hx + ny * hy + hz) / (nl * hl));
                const float spec = specStrength * std::pow (ndh, shininess);
                bd.setPixelColour (x, y, Colour::fromFloatRGBA (jlimit (0.0f, 1.0f, base.getFloatRed() * shade + spec),
                                                                jlimit (0.0f, 1.0f, base.getFloatGreen() * shade + spec * 0.97f),
                                                                jlimit (0.0f, 1.0f, base.getFloatBlue() * shade + spec * 0.9f), 1.0f));
            }
    }
    Graphics g (screen);
    g.addTransform (AffineTransform::scale (scale));
    const auto p = L.plate;
    const float pr = jmax (0.0f, L.cornerR - p.getX());             // the plate's corners follow the same curve, further in
    Path platePath; platePath.addRoundedRectangle (p, pr);
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (platePath);
        g.drawImage (plate, p);
        // the shadow the edge casts onto the plate, from the upper left
        for (int i = 1; i <= 7; ++i)
        {
            Path band (platePath);
            Path inner; inner.addRoundedRectangle (p.translated ((float) i, (float) i), pr);
            band.addPath (inner);
            band.setUsingNonZeroWinding (false);
            g.setColour (Colours::black.withAlpha (0.22f / 7.0f * 1.6f));
            g.fillPath (band);
        }
    }
    g.setColour (Colour (0xff070605));
    g.strokePath (platePath, PathStrokeType (1.8f));
    return screen;
}

// ------------------------------------------------------------------ the whole screen without its moving parts
static Image renderBackground (Layout& L, float scale)
{
    layoutCounter (L);
    const int W = (int) L.plate.getWidth(), H = (int) L.plate.getHeight();
    // the plate: the suite's base, print (worn), and life
    Random rng (2027);
    Image plate = buildPlateBase (W, H, scale, rng, false, ! L.fullScreen);
    {
        Graphics g (plate);
        // groove under the title, as on every plugin
        {
            Graphics::ScopedSaveState s (g);
            g.addTransform (AffineTransform::scale (scale));
            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillRect (14.0f, L.grooveY, (float) W - 28.0f, 1.6f);
            g.setColour (Colours::white.withAlpha (0.18f));
            g.fillRect (14.0f, L.grooveY + 1.6f, (float) W - 28.0f, 1.0f);
        }
        Image print (Image::ARGB, plate.getWidth(), plate.getHeight(), true);
        {
            Graphics pg (print);
            pg.addTransform (AffineTransform::scale (scale));
            pg.setColour (Palette::cream);
            pg.setFont (KintsugiFonts::serif (L.titleSize).withExtraKerningFactor (0.14f));
            pg.drawText ("DECK RA-1", Rectangle<float> (L.title.x, L.title.y, 300.0f, L.titleSize * 1.15f), Justification::centredLeft);
            for (auto& b : L.brackets) drawSectionBracket (pg, b.r, b.title, false);
            pg.setFont (KintsugiFonts::serif (11.5f).withExtraKerningFactor (0.2f));
            pg.drawText ("LINK", Rectangle<float> (60.0f, 14.0f).withCentre (L.linkLabel.translated (0.0f, 4.0f)), Justification::centred);
            // the maker's name, as on the plugins
            const auto m = L.maker;
            pg.setFont (KintsugiFonts::serif (8.5f).withExtraKerningFactor (0.42f));
            pg.drawText ("RELUCTANT", Rectangle<float> (m.getCentreX() - 110.0f, m.getY(), 220.0f, 12.0f), Justification::centred);
            pg.setFont (KintsugiFonts::serif (17.0f).withExtraKerningFactor (0.12f));
            pg.drawText ("ASTRONAUT", Rectangle<float> (m.getCentreX() - 130.0f, m.getY() + 10.0f, 260.0f, 22.0f), Justification::centred);
        }
        wearPrint (print, 78, 1.0f);
        g.drawImageAt (print, 0, 0);

        Graphics::ScopedSaveState s (g);
        g.addTransform (AffineTransform::scale (scale));
        const float w = (float) W, h = (float) H, pi = MathConstants<float>::pi;
        Random life (8128);
        for (int i = 0; i < (int) (w + h) / 3; ++i)   // edges rubbed by hands
        {
            const bool horiz = life.nextBool();
            const float t = life.nextFloat();
            const Point<float> p = horiz ? Point<float> (t * w, life.nextBool() ? life.nextFloat() * 5.0f : h - life.nextFloat() * 5.0f)
                                         : Point<float> (life.nextBool() ? life.nextFloat() * 5.0f : w - life.nextFloat() * 5.0f, t * h);
            g.setColour (Colour (0xffb7bbb1).withAlpha (0.10f + 0.18f * life.nextFloat()));
            g.fillEllipse (Rectangle<float> (1.0f + life.nextFloat() * 3.0f, 0.8f + life.nextFloat() * 1.6f).withCentre (p));
        }
        for (int i = 0; i < 14; ++i)   // longer scratches through the paint
        {
            const Point<float> a (life.nextFloat() * w, life.nextFloat() * h);
            const float len = 10.0f + life.nextFloat() * 40.0f, ang = life.nextFloat() * pi;
            Path sc;
            sc.startNewSubPath (a);
            sc.quadraticTo (a.getPointOnCircumference (len * 0.5f, ang + 0.05f), a.getPointOnCircumference (len, ang));
            g.setColour (Colour (0xffb9bcb4).withAlpha (0.12f + 0.2f * life.nextFloat()));
            g.strokePath (sc, PathStrokeType (0.35f + 0.3f * life.nextFloat()));
        }
        Repair::dent (g, { w - 11.0f, h * 0.62f }, 9.0f, 22.0f, 0.08f, 0.15f, 0.55f, 1.4f, 12);
        // brass corners holding the plate, as on Kintsugi: let in flush, riveted, a screw through each
        // (full screen: the phone's own rounded corners cut into them, so they are larger and the screws sit further in)
        const float si = L.fullScreen ? 28.0f : 15.0f;   // full screen: the phone's rounded corners need the screws further in
        Repair::screw (g, { si, si }, 0.4f);  Repair::screw (g, { w - si, si }, 0.7f);
        Repair::screw (g, { si, h - si }, 2.1f); Repair::screw (g, { w - si, h - si }, 1.3f);
        // finger wear around the keys you use most
        for (auto& k : L.keys)
            if (k.id == "play" || k.id == "stop" || k.id == "rec")
                Repair::fingerWear (g, k.r.getCentre(), jmax (k.r.getWidth(), k.r.getHeight()) * 0.45f, 1.4f, 3.2f, 1.0f);
        // the wells the keys sit in
        for (auto& k : L.keys)
        {
            g.setColour (Colours::black.withAlpha (0.55f));
            g.fillRoundedRectangle (k.r.expanded (1.8f), k.big ? 7.0f : 5.5f);
            g.setColour (Colours::white.withAlpha (0.10f));
            g.drawRoundedRectangle (k.r.expanded (2.3f).translated (0.4f, 0.6f), k.big ? 7.5f : 6.0f, 0.6f);
        }
        paintCounterBody (g, L);
    }

    if (L.fullScreen) return roundedEdge (L, plate, scale);
    // the case around it, the plate in its window, and the shadow the frame casts onto the plate
    Image screen = buildHousingImage (4, L.screenW, L.screenH, L.plate, scale, ! L.fullScreen);
    Graphics g (screen);
    g.drawImageAt (plate, (int) std::round (L.plate.getX() * scale), (int) std::round (L.plate.getY() * scale));
    g.addTransform (AffineTransform::scale (scale));
    const auto p = L.plate;
    for (int i = 0; i < 7; ++i)
    {
        const float a = 0.22f * (1.0f - (float) i / 7.0f);
        g.setColour (Colours::black.withAlpha (a));
        g.fillRect (p.getX(), p.getY() + (float) i, p.getWidth(), 1.0f);
        g.fillRect (p.getX() + (float) i, p.getY(), 1.0f, p.getHeight());
    }
    g.setColour (Colour (0xff070605));
    g.drawRect (p.expanded (1.2f), 1.8f);
    return screen;
}

// ------------------------------------------------------------------ output helpers
static void savePNG (const Image& img, const File& f)
{
    f.deleteFile();
    FileOutputStream os (f);
    PNGImageFormat png;
    png.writeImageToStream (img, os);
}
static Image sprite (Rectangle<float> area, float scale, std::function<void (Graphics&)> paint)
{
    Image img (Image::ARGB, (int) std::ceil (area.getWidth() * scale), (int) std::ceil (area.getHeight() * scale), true);
    Graphics g (img);
    g.addTransform (AffineTransform::translation (-area.getX(), -area.getY()).scaled (scale));
    paint (g);
    return img;
}
static float keyPad (const KeyDef& k) { return std::ceil (jmin (k.r.getWidth(), k.r.getHeight()) * 0.55f + 6.0f); }

// the whole screen with its moving parts in a given state, for the mock-ups
static Image composeScreen (Layout& L, float scale, const std::set<String>& litLamps, const std::set<String>& held, const String& time, bool frame)
{
    Image img = renderBackground (L, scale);
    Graphics g (img);
    g.addTransform (AffineTransform::scale (scale));
    g.addTransform (AffineTransform::translation (L.plate.getX(), L.plate.getY()));
    for (auto& k : L.keys) paintKey (g, k, held.count (k.id) > 0);
    for (auto& l : L.lamps)
    {
        const bool on = litLamps.count (l.id) > 0;
        Graphics::ScopedSaveState ss (g);
        if (l.colour == "link") g.addTransform (AffineTransform::scale (L.linkScale, L.linkScale, l.c.x, l.c.y));
        paintLamp (g, l.c, l.colour == "link" ? (on ? "green" : "amber") : l.colour, on ? 1.0f : 0.0f);
    }
    // the counter: right-aligned bars, then beat, sixteenth, ticks
    StringArray parts; parts.addTokens (time, ":", ""); parts.trim();
    String digits = parts[0].paddedLeft ('0', 2) + parts[1].paddedLeft ('0', 2) + parts[2].paddedLeft ('0', 2);
    for (size_t i = 0; i < L.drums.size() && i < (size_t) digits.length(); ++i)
    {
        const auto ch = digits[(int) i];
        const float pos = ch == ' ' ? 0.0f : (float) (ch - '0' + 1);
        paintDrumFace (g, L.drums[i], pos);
        paintDrumShade (g, L.drums[i]);
    }
    if (frame)
    {
        // the phone's own parts, so the safe areas show: Dynamic Island, status bar, home bar
        g.addTransform (AffineTransform::translation (-L.plate.getX(), -L.plate.getY()));
        g.setColour (Colours::black);
        if (L.name == "portrait")
        {
            g.fillRoundedRectangle (Rectangle<float> (126.0f, 37.0f).withCentre ({ L.screenW * 0.5f, 29.5f }), 18.5f);
            g.setColour (Colours::white);
            g.setFont (Font (FontOptions().withHeight (16.0f).withStyle ("Bold")));
            g.drawText ("21:45", Rectangle<float> (40.0f, 18.0f, 70.0f, 22.0f), Justification::centred);
            g.fillRoundedRectangle (Rectangle<float> (134.0f, 5.0f).withCentre ({ L.screenW * 0.5f, (float) L.screenH - 8.0f }), 2.5f);
        }
        else
        {
            g.fillRoundedRectangle (Rectangle<float> (37.0f, 126.0f).withCentre ({ 29.5f, L.screenH * 0.5f }), 18.5f);
            g.setColour (Colours::white);
            g.fillRoundedRectangle (Rectangle<float> (134.0f, 5.0f).withCentre ({ L.screenW * 0.5f, (float) L.screenH - 8.0f }), 2.5f);
        }
    }
    return img;
}

// rounded phone outline around a screen, for the mock-up sheet
static Image phoneFrame (const Image& screen, float scale)
{
    const int bezel = (int) (14 * scale);
    Image out (Image::ARGB, screen.getWidth() + 2 * bezel, screen.getHeight() + 2 * bezel, true);
    Graphics g (out);
    const auto all = out.getBounds().toFloat();
    g.setColour (Colour (0xff111111));
    g.fillRoundedRectangle (all, 62.0f * scale);
    g.setColour (Colour (0xff555555));
    g.drawRoundedRectangle (all.reduced (1.5f * scale), 61.0f * scale, 2.0f * scale);
    Path clip; clip.addRoundedRectangle (all.reduced ((float) bezel), 50.0f * scale);
    g.reduceClipRegion (clip);
    g.drawImageAt (screen, bezel, bezel);
    return out;
}

static String layoutJSON (Layout& L)
{
    auto num = [] (float v) { return String (v, 2); };
    auto rect = [&] (Rectangle<float> r) { return "[" + num (r.getX()) + "," + num (r.getY()) + "," + num (r.getWidth()) + "," + num (r.getHeight()) + "]"; };
    String s = "{\"screen\":[" + String (L.screenW) + "," + String (L.screenH) + "],\"plate\":" + rect (L.plate) + ",\"linkScale\":" + num (L.linkScale) + ",\"keys\":[";
    for (size_t i = 0; i < L.keys.size(); ++i)
    {
        auto& k = L.keys[i];
        s << (i ? "," : "") << "{\"id\":\"" << k.id << "\",\"cc\":" << k.cc << ",\"r\":" << rect (k.r) << ",\"pad\":" << num (keyPad (k)) << "}";
    }
    s << "],\"lamps\":[";
    for (size_t i = 0; i < L.lamps.size(); ++i)
    {
        auto& l = L.lamps[i];
        s << (i ? "," : "") << "{\"id\":\"" << l.id << "\",\"cc\":" << l.cc << ",\"colour\":\"" << l.colour << "\",\"c\":[" << num (l.c.x) << "," << num (l.c.y) << "]}";
    }
    s << "],\"drums\":[";
    for (size_t i = 0; i < L.drums.size(); ++i) s << (i ? "," : "") << rect (L.drums[i]);
    s << "]}";
    return s;
}

static Image renderIcon (int size)
{
    // the Home Screen icon: a corner of the plate in its hammertone case, DECK over RA-1 in the suite's print, a lit key below
    const float scale = (float) size / 180.0f;
    Layout L; L.screenW = 180; L.screenH = 180; L.plate = { 12.0f, 12.0f, 156.0f, 156.0f };
    Random rng (2027);
    Image plate = buildPlateBase (156, 156, scale, rng, false, false);
    {
        Graphics g (plate);
        Image print (Image::ARGB, plate.getWidth(), plate.getHeight(), true);
        {
            Graphics pg (print);
            pg.addTransform (AffineTransform::scale (scale));
            pg.setColour (Palette::cream);
            pg.setFont (KintsugiFonts::serif (40.0f).withExtraKerningFactor (0.10f));
            pg.drawText ("DECK", Rectangle<float> (0.0f, 22.0f, 160.0f, 44.0f), Justification::centred);
            pg.fillRect (34.0f, 72.0f, 88.0f, 1.5f);
            pg.setFont (KintsugiFonts::serif (19.0f).withExtraKerningFactor (0.30f));
            pg.drawText ("RA-1", Rectangle<float> (0.0f, 78.0f, 160.0f, 24.0f), Justification::centred);
        }
        wearPrint (print, 78, 0.8f);
        g.drawImageAt (print, 0, 0);
        g.addTransform (AffineTransform::scale (scale));
        KeyDef play { "play", 23, "PLAY", "play", { 54.0f, 112.0f, 48.0f, 30.0f }, false };
        g.setColour (Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (play.r.expanded (1.8f), 5.5f);
        paintKey (g, play, false);
        paintLamp (g, { 122.0f, 127.0f }, "green", 1.0f);
        Repair::screw (g, { 20.0f, 20.0f }, 0.4f); Repair::screw (g, { 136.0f, 20.0f }, 0.7f);   // clear of the rounded icon corners
        Repair::screw (g, { 20.0f, 136.0f }, 2.1f); Repair::screw (g, { 136.0f, 136.0f }, 1.3f);
    }
    Image icon = buildHousingImage (4, 180, 180, L.plate, scale, false);
    Graphics g (icon);
    g.drawImageAt (plate, (int) std::round (12 * scale), (int) std::round (12 * scale));
    return icon.rescaled (size, size, Graphics::highResamplingQuality);
}
} // namespace Deck

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    using namespace Deck;
    const String mode = argc > 1 ? argv[1] : "mock";
    const File out (File::getCurrentWorkingDirectory().getChildFile (argc > 2 ? argv[2] : "deck-out"));
    out.createDirectory();
    const float scale = 3.0f;   // iPhone 15: 3 pixels per point

    if (mode == "mock")
    {
        Deck::Layout Lnd = landscape(), L2 = landscape();
        savePNG (phoneFrame (composeScreen (Lnd, scale, { "link", "play" }, { "play" }, "", true), scale), out.getChildFile ("Deck-RA1-landscape.png"));
        savePNG (phoneFrame (composeScreen (L2, scale, { "link", "play", "rec" }, { "play", "rec" }, "", true), scale), out.getChildFile ("Deck-RA1-landscape-recording.png"));
        std::cout << "mock-ups written to " << out.getFullPathName() << std::endl;
        return 0;
    }
    if (mode == "screen")   // just the panel, at the iPhone 15's own resolution (2556 x 1179), no phone around it
    {
        Deck::Layout L1 = landscape(), L2 = landscape();
        savePNG (composeScreen (L1, scale, { "link", "play" }, { "play" }, "", false), out.getChildFile ("Deck-RA1-screen.png"));
        savePNG (composeScreen (L2, scale, { "link" }, {}, "", false), out.getChildFile ("Deck-RA1-screen-stopped.png"));
        Deck::Layout L3 = landscape();
        savePNG (composeScreen (L3, scale, { "link", "play", "rec" }, { "play", "rec" }, "", false), out.getChildFile ("Deck-RA1-screen-recording.png"));
        Deck::Layout F1 = ipadFull();
        savePNG (composeScreen (F1, 2.0f, { "link", "play", "rec" }, { "play", "rec", "arm", "click" }, "", false), out.getChildFile ("Deck-RA1-iPad-full.png"));
        Deck::Layout I1 = ipad(), I2 = ipad();
        savePNG (composeScreen (I1, 2.0f, { "link", "play" }, { "play" }, "", false), out.getChildFile ("Deck-RA1-iPad-screen.png"));
        savePNG (composeScreen (I2, 2.0f, { "link", "play", "rec" }, { "play", "rec" }, "", false), out.getChildFile ("Deck-RA1-iPad-screen-recording.png"));
        std::cout << "screens written to " << out.getFullPathName() << std::endl;
        return 0;
    }
    if (mode == "assets")
    {
        String json = "{";
        bool first = true;
        for (auto L : { landscape(), ipadFull() })
        {
            const float scale = L.pxScale;
            auto bg = renderBackground (L, scale);
            savePNG (bg, out.getChildFile ("bg-" + L.name + ".png"));
            for (auto& k : L.keys)
                for (int lit = 0; lit < 2; ++lit)
                {
                    const float pad = keyPad (k);
                    auto img = sprite (k.r.expanded (pad), scale, [&] (Graphics& g) { paintKey (g, k, lit == 1); });
                    savePNG (img, out.getChildFile ("key-" + L.name + "-" + k.id + (lit ? "-down" : "-up") + ".png"));
                }
            if (! L.drums.empty())
            {
                // one drum, every digit stacked (blank, 0-9), and the shade that goes over it
                const auto ap = L.drums[0];
                const float itemH = ap.getHeight() * 0.95f;
                Rectangle<float> strip (0.0f, 0.0f, ap.getWidth(), itemH * 11.0f);
                auto st = sprite (strip, scale, [&] (Graphics& g)
                {
                    g.setColour (Colour (0xffe9dcb8)); g.fillRect (strip);
                    for (int i = 0; i < 11; ++i)
                    {
                        g.setColour (Colour (0xff1c1812));
                        g.setFont (KintsugiFonts::serif (ap.getHeight() * 0.78f, true));
                        g.drawText (drumItems[i], Rectangle<float> (0.0f, (float) i * itemH, ap.getWidth(), itemH), Justification::centred);
                    }
                });
                savePNG (st, out.getChildFile ("drum-strip-" + L.name + ".png"));
                auto sh = sprite ({ 0.0f, 0.0f, ap.getWidth(), ap.getHeight() }, scale, [&] (Graphics& g) { paintDrumShade (g, { 0.0f, 0.0f, ap.getWidth(), ap.getHeight() }); });
                savePNG (sh, out.getChildFile ("drum-shade-" + L.name + ".png"));
            }
            json << (first ? "" : ",") << "\"" << L.name << "\":" << layoutJSON (L).dropLastCharacters (1) << ",\"scale\":" << String (L.pxScale, 0) << "}";
            first = false;
        }
        for (auto c : { "amber", "red", "green" })
            for (int lit = 0; lit < 2; ++lit)
            {
                auto img = sprite ({ -22.0f, -22.0f, 44.0f, 44.0f }, scale, [&] (Graphics& g) { paintLamp (g, { 0.0f, 0.0f }, c, (float) lit); });
                savePNG (img, out.getChildFile (String ("lamp-") + c + (lit ? "-on" : "-off") + ".png"));
            }
        json << ",\"drumItemRatio\":0.95,\"scale\":3}";
        out.getChildFile ("layout.json").replaceWithText (json);
        savePNG (renderIcon (180), out.getChildFile ("icon-180.png"));
        savePNG (renderIcon (512), out.getChildFile ("icon-512.png"));
        std::cout << "assets written to " << out.getFullPathName() << std::endl;
        return 0;
    }
    std::cerr << "usage: DeckRender mock|assets <dir>" << std::endl;
    return 1;
}
