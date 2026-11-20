"""Builds Seed.swift (the app's starting data) and preview/seed.js from one definition."""
import json

def pid(name):
    return name.lower().replace(' ', '-').replace('/', '-').replace('(', '').replace(')', '')

def P(name, d, conn, members=None):
    p = {"id": pid(name), "name": name, "direction": "input" if d == 'i' else "output", "connector": conn, "members": []}
    if members:
        p["members"] = [pid(m) for m in members]
    return p

def pair(name, d, conn, a, b):
    return P(name, d, conn, [a, b])

devices = []
def D(id, name, maker="", model="", cat="other", loc=None, status="in use", patchable=True, ports=(), notes="", year=""):
    devices.append({"id": id, "name": name, "maker": maker, "model": model, "category": cat, "serial": "",
                    "yearMade": year, "whereBought": "", "location": loc, "status": status, "notes": notes,
                    "patchable": patchable, "ports": list(ports), "settings": {}, "photos": [], "snapshots": [], "care": []})

R = range
# --- Interfaces ---
ssl = [P(f"In {n}", 'i', "XLR/TRS combo") for n in R(1, 9)]
ssl += [pair(f"In {a}+{a+1}", 'i', "XLR/TRS combo", f"In {a}", f"In {a+1}") for a in (1, 3, 5, 7)]
ssl += [P("Inst 1 (front)", 'i', "TS"), P("Inst 2 (front)", 'i', "TS"), P("Talkback In", 'i', "TRS"),
        P("ADAT In A", 'i', "ADAT"), P("ADAT In B", 'i', "ADAT"), P("S/PDIF In", 'i', "S/PDIF"), P("MIDI In", 'i', "MIDI")]
ssl += [P(f"Out {n}", 'o', "TRS") for n in R(1, 11)]
ssl += [pair(f"Out {a}+{a+1}", 'o', "TRS", f"Out {a}", f"Out {a+1}") for a in (1, 3, 5, 7, 9)]
ssl += [P("HP A", 'o', "TRS"), P("HP B", 'o', "TRS"), P("ADAT Out A", 'o', "ADAT"), P("ADAT Out B", 'o', "ADAT"),
        P("S/PDIF Out", 'o', "S/PDIF"), P("MIDI Out", 'o', "MIDI"), P("USB-C", 'o', "USB")]
D("ssl18", "SSL 18", "Solid State Logic", "SSL 18", "interface", "desk", ports=ssl, notes="Primary interface. Clock master.")

motu = [P("Mic/Gtr 1 (front)", 'i', "XLR/TRS combo"), P("Mic/Gtr 2 (front)", 'i', "XLR/TRS combo")]
motu += [P(f"Line In {n}", 'i', "TRS") for n in R(1, 9)]
motu += [pair(f"Line In {a}+{a+1}", 'i', "TRS", f"Line In {a}", f"Line In {a+1}") for a in (1, 3, 5, 7)]
motu += [P("ADAT In (optical)", 'i', "ADAT"), P("S/PDIF In (RCA)", 'i', "S/PDIF"), P("Word Clock In", 'i', "Other"), P("MIDI In", 'i', "MIDI")]
motu += [P("Main Out L", 'o', "TRS"), P("Main Out R", 'o', "TRS"), pair("Main Out L+R", 'o', "TRS", "Main Out L", "Main Out R")]
motu += [P(f"Line Out {n}", 'o', "TRS") for n in R(1, 9)]
motu += [pair(f"Line Out {a}+{a+1}", 'o', "TRS", f"Line Out {a}", f"Line Out {a+1}") for a in (1, 3, 5, 7)]
motu += [P("Phones", 'o', "TRS"), P("Mic Send 1", 'o', "TRS"), P("Mic Send 2", 'o', "TRS"), P("ADAT Out (optical)", 'o', "ADAT"),
         P("S/PDIF Out (RCA)", 'o', "S/PDIF"), P("Word Clock Out", 'o', "Other"), P("MIDI Out", 'o', "MIDI"), P("FireWire", 'o', "Other")]
D("motu828", "MOTU 828 MkII", "MOTU", "828mkII (FireWire)", "interface", "rack", ports=motu, notes="ADAT expander, clocked from the SSL 18.")

# --- Preamps & outboard ---
D("isaone", "Focusrite ISA One", "Focusrite", "ISA One", "preamp", "rack", ports=[
    P("Mic In (XLR)", 'i', "XLR"), P("Line In (XLR)", 'i', "XLR"), P("Line In (TRS)", 'i', "TRS"), P("Inst In (front)", 'i', "TS"),
    P("Insert Return", 'i', "TRS"), P("Ext In (ADC 2)", 'i', "TRS"), P("Cue In L", 'i', "TRS"), P("Cue In R", 'i', "TRS"),
    pair("Cue In L+R", 'i', "TRS", "Cue In L", "Cue In R"),
    P("Main Out (XLR)", 'o', "XLR"), P("DI Out (XLR)", 'o', "XLR"), P("Amp Out / DI Thru", 'o', "TS"), P("Insert Send", 'o', "TRS"),
    P("Headphones", 'o', "TRS")])
D("voicemaster", "Focusrite VoiceMaster Pro", "Focusrite", "Platinum VoiceMaster Pro", "preamp", "rack", ports=[
    P("Mic In (rear XLR)", 'i', "XLR"), P("Mic In (front XLR)", 'i', "XLR"), P("Line In (TRS)", 'i', "TRS"), P("Inst In (front)", 'i', "TS"),
    P("Insert Return", 'i', "TRS"), P("FX Return L", 'i', "TS"), P("FX Return R", 'i', "TS"), pair("FX Return L+R", 'i', "TS", "FX Return L", "FX Return R"),
    P("Monitor In L", 'i', "TRS"), P("Monitor In R", 'i', "TRS"), pair("Monitor In L+R", 'i', "TRS", "Monitor In L", "Monitor In R"),
    P("Main Out (XLR +4)", 'o', "XLR"), P("Main Out (jack -10)", 'o', "TS"), P("Pre De-esser Out (XLR)", 'o', "XLR"), P("Insert Send", 'o', "TRS"),
    P("FX Send", 'o', "TS"), P("Monitor Out L", 'o', "TRS"), P("Monitor Out R", 'o', "TRS"), pair("Monitor Out L+R", 'o', "TRS", "Monitor Out L", "Monitor Out R"),
    P("Headphones", 'o', "TRS")], notes="Part of the pedal rig: Marine Layer → VoiceMaster → SSL In 6.")
D("wa76", "WA76 A2", "Warm Audio", "WA76 A2 (stereo)", "compressor", "rack", ports=[
    P("In L", 'i', "XLR/TRS combo"), P("In R", 'i', "XLR/TRS combo"), pair("In L+R", 'i', "XLR/TRS combo", "In L", "In R"),
    P("Out L", 'o', "XLR"), P("Out R", 'o', "XLR"), pair("Out L+R", 'o', "XLR", "Out L", "Out R")],
    notes="External FX: SSL Out 7+8 → WA76 → SSL In 7+8.")
D("nts1", "Korg NTS-1 MkII", "Korg", "NTS-1 digital kit mkII", "synth", "desk", ports=[
    P("Audio In (3.5mm)", 'i', "TRS"), P("MIDI In", 'i', "MIDI"), P("Out (3.5mm stereo)", 'o', "TRS"), P("Headphones", 'o', "TRS")])
D("marine", "Fender Marine Layer Reverb", "Fender", "Marine Layer Reverb", "pedal", "desk", ports=[P("In", 'i', "TS"), P("Out", 'o', "TS")],
  notes="Pedal rig: SSL Out 6 → Marine Layer → VoiceMaster Pro.")
D("terror", "Orange Terror Stamp", "Orange", "Terror Stamp", "pedal", ports=[
    P("Guitar In", 'i', "TS"), P("FX Return", 'i', "TS"), P("Speaker Out", 'o', "Speaker"), P("FX Send", 'o', "TS"),
    P("Headphone / Cab-sim Out", 'o', "TRS")])

# --- Mics ---
D("k2", "Røde K2", "Røde", "K2", "mic", ports=[P("XLR Out (via PSU)", 'o', "XLR")], notes="Valve mic; output comes from its power supply.")
D("wa84", "WA84 stereo pair", "Warm Audio", "WA-84 (stereo pair)", "mic", ports=[P("Mic A", 'o', "XLR"), P("Mic B", 'o', "XLR")])
D("sm57", "Shure SM57", "Shure", "SM57", "mic", ports=[P("XLR Out", 'o', "XLR")])
D("cm300", "Korg CM300 contact mics", "Korg", "CM-300", "mic", ports=[P("Mic 1", 'o', "TS"), P("Mic 2", 'o', "TS")],
  notes="Used as pickups on the DIY spring reverb.")

# --- Monitoring & listening ---
D("ndh30", "Neumann NDH 30", "Neumann", "NDH 30", "headphones", "control room", ports=[P("Cable In", 'i', "TRS")])
D("krk", "KRK V4 pair", "KRK", "V4 (vintage)", "speaker", "control room", ports=[
    P("In L", 'i', "XLR/TRS combo"), P("In R", 'i', "XLR/TRS combo"), pair("In L+R", 'i', "XLR/TRS combo", "In L", "In R")])
D("philips", "Philips GL124 pair", "Philips", "GL124", "speaker", "control room", ports=[P("Speaker L+R", 'i', "Speaker")],
  notes="Secondary pair, driven by the ZK-100 amp.")
D("zk100", "ZK-100 2MT amp", "", "ZK-100 2MT (Bluetooth)", "other", "control room", ports=[
    P("Line In", 'i', "TRS"), P("Speaker Out L+R", 'o', "Speaker")],
  notes="Small Bluetooth amp for the Philips pair. Fed from SSL HP B (Line config).")
D("monicon", "Palmer Monicon L", "Palmer", "Monicon L", "other", "desk", ports=[
    P("Input L", 'i', "TRS"), P("Input R", 'i', "TRS"), pair("Input L+R", 'i', "TRS", "Input L", "Input R"),
    P("Output L", 'o', "TRS"), P("Output R", 'o', "TRS"), pair("Output L+R", 'o', "TRS", "Output L", "Output R")],
  notes="Monitor controller. Preferred for hands-on level control.")
D("teac", "Teac MB-20 VU bridge", "TEAC", "MB-20", "other", "desk", ports=[
    P("In L", 'i', "RCA"), P("In R", 'i', "RCA"), pair("In L+R", 'i', "RCA", "In L", "In R")],
  notes="Fed from its own SSL outputs (3+4), only for the meters. Not inline: it caused a weak left channel.")

# --- Playback ---
D("tandberg", "Tandberg 3B", "Tandberg", "3B reel-to-reel", "playback", "tracking corner", ports=[
    P("Line In", 'i', "Other"), P("Mic In", 'i', "Other"), P("Line Out", 'o', "Other")])
D("garrard", "Garrard SRP22", "Garrard", "SRP22", "playback", "tracking corner", patchable=False, ports=[P("Phono Out", 'o', "RCA")])

# --- DIY ---
D("voxcab", "Homemade guitar cab (Vox VK10)", "", "Vox VK10 speaker", "diy", ports=[P("Speaker In", 'i', "Speaker")])
D("spring", "DIY spring reverb", "", "Baking tray + DAEX exciter", "diy", ports=[P("Exciter In", 'i', "Speaker"), P("Pickup Out", 'o', "TS")],
  notes="Picked up with the CM300 contact mics into the ISA One.")

# --- Instruments (not patchable until switched on) ---
D("greenie", "Les Paul “Greenie”", "", "Les Paul tribute", "instrument", "guitar wall", patchable=False, ports=[P("Jack", 'o', "TS")])
D("pbass", "Japanese vintage P-bass", "", "Precision-style bass", "instrument", "guitar wall", patchable=False, ports=[P("Jack", 'o', "TS")])
D("nylon", "Nylon-string acoustic", "", "", "instrument", "guitar wall", patchable=False)
D("takamine", "Takamine EG540C", "Takamine", "EG540C", "instrument", "guitar wall", patchable=False, ports=[P("Jack", 'o', "TS")])
D("parlour", "Vintage parlour guitar", "", "Parlour guitar (R·D “Ridi” globe label)", "instrument", status="under repair", patchable=False,
  year="c. 1890s–1920s", notes="Conservation and repair project. Goal: preserve it, and maybe play it.")
D("piano", "Weinbach upright piano", "Weinbach", "Upright piano", "instrument", patchable=False)
D("bontempi", "Bontempi HiT Organ", "Bontempi", "HiT Organ HO 3712 C", "instrument", patchable=False, year="1970s",
  notes="Swedish-market reed chord organ, 22 keys. Plans: sample it, then mod it (wooden frame, fan-speed control).")

# --- Infrastructure ---
D("mac", "Mac mini", "Apple", "Mac mini (Apple silicon)", "infrastructure", "control room", ports=[P("USB-C", 'i', "USB")], notes="Cubase 14 Pro.")
D("t9", "Samsung T9 SSD", "Samsung", "Portable SSD T9, 1 TB", "infrastructure", "desk", patchable=False,
  notes="Working drive for active Cubase sessions.")

# --- Cables ---
conns = []
def C(fd, fp, td, tp, cable, status="active", notes=""):
    conns.append({"id": f"c{len(conns)+1}", "from": {"dev": fd, "port": pid(fp) if fp else ""},
                  "to": {"dev": td, "port": pid(tp) if tp else ""}, "cable": cable, "status": status,
                  "verified": False, "notes": notes})

C("ssl18", "Out 1+2", "monicon", "Input L+R", "TRS")
C("monicon", "Output L+R", "krk", "In L+R", "TRS")
C("ssl18", "HP A", "ndh30", "Cable In", "TRS")
C("ssl18", "HP B", "zk100", "Line In", "TRS", notes="HP B in Line config.")
C("zk100", "Speaker Out L+R", "philips", "Speaker L+R", "Speaker")
C("ssl18", "Out 3+4", "teac", "In L+R", "TRS", notes="Meters only.")
C("ssl18", "Out 7", "wa76", "In L", "TRS")
C("ssl18", "Out 8", "wa76", "In R", "TRS")
C("wa76", "Out L", "ssl18", "In 7", "XLR")
C("wa76", "Out R", "ssl18", "In 8", "XLR")
C("isaone", "Main Out (XLR)", "ssl18", "In 5", "XLR")
C("ssl18", "Out 6", "marine", "In", "TS", notes="Pedal rig send.")
C("marine", "Out", "voicemaster", "Line In (TRS)", "TS")
C("voicemaster", "Main Out (XLR +4)", "ssl18", "In 6", "XLR", notes="Pedal rig return.")
C("motu828", "ADAT Out (optical)", "ssl18", "ADAT In A", "ADAT")
C("ssl18", "ADAT Out A", "motu828", "ADAT In (optical)", "ADAT", notes="Carries clock — SSL 18 is master.")
C("cm300", "Mic 1", "isaone", "Mic In (XLR)", "TS", notes="Spring-reverb pickup rig.")
C("ssl18", "USB-C", "mac", "USB-C", "USB")
C("nts1", "Out (3.5mm stereo)", "motu828", "Line In 1+2", "TRS", "planned", "Stereo pair into the MOTU.")
ex = 'Example chain — set to "Connected now" when patched, or delete.'
C("greenie", "Jack", "terror", "Guitar In", "TS", "planned", ex)
C("terror", "Speaker Out", "voxcab", "Speaker In", "Speaker", "planned", ex)
C("voxcab", "", "sm57", "", "Mic'd (acoustic)", "planned", ex)
C("sm57", "XLR Out", "isaone", "Mic In (XLR)", "XLR", "planned", ex)

# --- Panel settings that follow from the patching ---
S = {d["id"]: d for d in devices}
S["ssl18"]["settings"] = {
    "ch5.mode": "Line", "ch5.label": "ISA One",
    "ch6.mode": "Line", "ch6.label": "Pedal rig (VoiceMaster)",
    "ch7.mode": "Line", "ch7.label": "WA76 L",
    "ch8.mode": "Line", "ch8.label": "WA76 R",
}
S["motu828"]["settings"] = {"clock": "ADAT"}
S["voicemaster"]["settings"] = {"in.src": "Line"}

state = {"v": 3, "owner": {"name": "", "address": ""}, "devices": devices, "conns": conns, "log": []}

# sanity: every cable points at real jacks
for c in conns:
    for side in ("from", "to"):
        e = c[side]
        d = S[e["dev"]]
        if e["port"]:
            assert any(p["id"] == e["port"] for p in d["ports"]), (c, side)
assert len(devices) == 31, len(devices)

txt = json.dumps(state, ensure_ascii=False, indent=1)
assert '"#' not in txt
open('/home/claude/PatchBook-mac/Sources/PatchBook/Seed.swift', 'w').write(
    '// Starting state for One Bright Pearl Studio (used on first launch only).\n'
    '// Generated by build_seed.py — details like serials, prices and photos are added in the app.\n'
    'let seedJSON = #"""\n' + txt + '\n"""#\n')
open('/home/claude/preview/seed.js', 'w').write('const SEED=' + txt + ';')
print(len(devices), "devices,", len(conns), "cables")
