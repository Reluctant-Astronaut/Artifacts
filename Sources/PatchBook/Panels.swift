import SwiftUI

// MARK: - Panel building blocks
//
// A device panel is described as data: channel strips (each with its own switches and choices),
// rows of jacks (showing what is patched to them) and unit-wide settings. One view draws them all,
// so adding a panel for a new piece of gear only means adding a definition below.

enum ControlKind {
    case toggle(Color)
    case choice([String])
    case text(String)          // placeholder
}

struct PanelControl: Identifiable {
    let key: String            // full storage key, e.g. "ch1.p48"
    let label: String          // "48V"; empty for a channel's main mode switch
    let kind: ControlKind
    let defaultValue: String
    var alwaysRemind: Bool = false   // list it after a reset even when it is at the default
    var id: String { key }

    var isToggle: Bool { if case .toggle = kind { return true }; return false }
    var isText: Bool { if case .text = kind { return true }; return false }
}

struct PanelChannel: Identifiable {
    let key: String            // "ch1"
    let title: String          // "1"
    let subtitle: String       // "MIC·LINE·INST"
    let scope: String          // how the reset list names it: "Ch 1"
    let inPorts: [String]      // jacks on this device that feed the channel
    let controls: [PanelControl]
    var dashed: Bool = false
    var emptyText: String = "nothing patched"
    var id: String { key }
    var labelKey: String { key + ".label" }
}

struct PanelPortRow: Identifiable {
    let title: String
    let ports: [String]
    let side: Side             // .to = jack is an input on this device, .from = an output
    var note: String = ""      // e.g. what the jack is for
    var annotate: ((_ value: (String) -> String) -> String?)? = nil
    var id: String { title }
}

enum PanelSection: Identifiable {
    case strips(String, [PanelChannel], CGFloat)
    case ports(String, [PanelPortRow])
    case settings(String, [PanelControl])

    var id: String {
        switch self {
        case .strips(let t, _, _): return t
        case .ports(let t, _): return t
        case .settings(let t, _): return t
        }
    }
}

struct DevicePanel: Identifiable {
    let deviceID: String
    let resetTitle: String
    let resetNote: String
    let sections: [PanelSection]
    var id: String { deviceID }

    /// Every control with the name the reset list should use for its group.
    var scopedControls: [(scope: String, control: PanelControl)] {
        var out: [(scope: String, control: PanelControl)] = []
        for s in sections {
            switch s {
            case .strips(_, let chans, _):
                for ch in chans { for c in ch.controls { out.append((scope: ch.scope, control: c)) } }
            case .settings(_, let controls):
                for c in controls { out.append((scope: "", control: c)) }
            case .ports:
                break
            }
        }
        return out
    }

    var channels: [PanelChannel] {
        sections.flatMap { s -> [PanelChannel] in
            if case .strips(_, let chans, _) = s { return chans }
            return []
        }
    }
}

// MARK: - Control helpers

private func toggle(_ ch: String, _ key: String, _ label: String, _ color: Color) -> PanelControl {
    PanelControl(key: "\(ch).\(key)", label: label, kind: .toggle(color), defaultValue: "0")
}

private func choice(_ key: String, _ label: String, _ options: [String], remind: Bool = false) -> PanelControl {
    PanelControl(key: key, label: label, kind: .choice(options), defaultValue: options[0], alwaysRemind: remind)
}

private func note(_ key: String, _ label: String, _ placeholder: String) -> PanelControl {
    PanelControl(key: key, label: label, kind: .text(placeholder), defaultValue: "")
}

// MARK: - The panels

enum Panels {
    static let all: [DevicePanel] = [ssl18, isaOne, motu828, voiceMaster]

    static func panel(for deviceID: String) -> DevicePanel? {
        all.first { $0.deviceID == deviceID }
    }

    // SSL 18 ---------------------------------------------------------------

    static let ssl18: DevicePanel = {
        var strips: [PanelChannel] = (1...8).map { n -> PanelChannel in
            let k = "ch\(n)"
            var controls: [PanelControl] = [
                choice("\(k).mode", "", n <= 2 ? ["Mic", "Line", "Inst"] : ["Mic", "Line"]),
                toggle(k, "p48", "48V", Theme.red),
                toggle(k, "hpf", "HPF", Theme.white),
                toggle(k, "ph", "Ø", Theme.blue),
                toggle(k, "k4", "4K", Theme.amber)
            ]
            if n <= 2 { controls.append(toggle(k, "ins", "INSERT", Theme.violet)) }
            return PanelChannel(key: k, title: "\(n)", subtitle: n <= 2 ? "MIC·LINE·INST" : "MIC·LINE",
                                scope: "Ch \(n)",
                                inPorts: n <= 2 ? ["In \(n)", "Inst \(n) (front)"] : ["In \(n)"],
                                controls: controls)
        }
        strips.append(PanelChannel(key: "tb", title: "TB", subtitle: "TALKBACK", scope: "Talkback",
                                   inPorts: ["Talkback In"],
                                   controls: [choice("tb.mode", "", ["Mic", "Line"])],
                                   dashed: true, emptyText: "built-in mic"))
        let outs: [PanelPortRow] = (1...10).map { n -> PanelPortRow in
            var row = PanelPortRow(title: "OUT \(n)", ports: ["Out \(n)"], side: .from)
            if n >= 9 {
                row.annotate = { value in value("ch\(n - 8).ins") == "1" ? "Insert send, ch \(n - 8)" : nil }
            }
            return row
        }
        return DevicePanel(
            deviceID: "ssl18",
            resetTitle: "After a reset — set these in SSL 360",
            resetNote: "Everything not listed should sit at default (Mic mode, all switches off). Gain isn't stored in SSL 360 — it's the front-panel knobs.",
            sections: [
                .strips("Inputs", strips, 132),
                .settings("Output levels", [
                    choice("out12", "Out 1–2 level", ["+24 dBu", "+9 dBu"], remind: true),
                    choice("out34", "Out 3–4 level", ["+24 dBu", "+9 dBu"], remind: true)
                ]),
                .ports("Outputs", outs),
                .ports("Headphones, digital & USB", [
                    PanelPortRow(title: "HP A", ports: ["HP A"], side: .from),
                    PanelPortRow(title: "HP B", ports: ["HP B"], side: .from),
                    PanelPortRow(title: "ADAT OUT A", ports: ["ADAT Out A"], side: .from),
                    PanelPortRow(title: "ADAT OUT B", ports: ["ADAT Out B"], side: .from),
                    PanelPortRow(title: "ADAT IN A", ports: ["ADAT In A"], side: .to),
                    PanelPortRow(title: "ADAT IN B", ports: ["ADAT In B"], side: .to),
                    PanelPortRow(title: "S/PDIF OUT", ports: ["S/PDIF Out"], side: .from),
                    PanelPortRow(title: "S/PDIF IN", ports: ["S/PDIF In"], side: .to),
                    PanelPortRow(title: "MIDI OUT", ports: ["MIDI Out"], side: .from),
                    PanelPortRow(title: "MIDI IN", ports: ["MIDI In"], side: .to),
                    PanelPortRow(title: "USB-C", ports: ["USB-C"], side: .from)
                ])
            ])
    }()

    // Focusrite ISA One -------------------------------------------------------

    static let isaOne = DevicePanel(
        deviceID: "isaone",
        resetTitle: "Current settings to set on the front panel",
        resetNote: "Anything not listed is at its usual position: Mic selected, switches off, impedance on ISA 110.",
        sections: [
            .strips("Channels", [
                PanelChannel(key: "mic", title: "MIC / LINE", subtitle: "TRANSFORMER PRE", scope: "Mic/Line",
                             inPorts: ["Mic In (XLR)", "Line In (XLR)", "Line In (TRS)"],
                             controls: [
                                choice("mic.src", "", ["Mic", "Line"]),
                                choice("mic.imp", "Impedance", ["ISA 110 · 1.4k", "Low · 600", "Med · 2.4k", "High · 6.8k"]),
                                toggle("mic", "p48", "48V", Theme.red),
                                toggle("mic", "ph", "Ø", Theme.blue),
                                toggle("mic", "hpf", "HPF 75", Theme.white),
                                toggle("mic", "ins", "INSERT", Theme.violet),
                                note("mic.gain", "Gain / trim", "e.g. 30 dB, trim 2 o'clock")
                             ]),
                PanelChannel(key: "di", title: "DI", subtitle: "INSTRUMENT · FRONT", scope: "DI",
                             inPorts: ["Inst In (front)"],
                             controls: [
                                choice("di.imp", "Inst impedance", ["Hi", "Lo"]),
                                note("di.gain", "DI gain", "e.g. 2 o'clock")
                             ],
                             emptyText: "nothing plugged in front")
            ], 260),
            .ports("Outputs", [
                PanelPortRow(title: "MAIN OUT", ports: ["Main Out (XLR)"], side: .from, note: "XLR · mic/line path"),
                PanelPortRow(title: "DI OUT", ports: ["DI Out (XLR)"], side: .from, note: "XLR · separate DI path"),
                PanelPortRow(title: "AMP OUT", ports: ["Amp Out / DI Thru"], side: .from, note: "Front jack · DI thru to an amp"),
                PanelPortRow(title: "HEADPHONES", ports: ["Headphones"], side: .from)
            ]),
            .ports("Insert & extra inputs", [
                PanelPortRow(title: "INS SEND", ports: ["Insert Send"], side: .from, note: "TRS · active when INSERT is on"),
                PanelPortRow(title: "INS RETURN", ports: ["Insert Return"], side: .to, note: "TRS"),
                PanelPortRow(title: "EXT IN", ports: ["Ext In (ADC 2)"], side: .to, note: "TRS · feeds converter ch 2 (option card)"),
                PanelPortRow(title: "CUE IN", ports: ["Cue In L", "Cue In R"], side: .to, note: "TRS L/R · headphone cue mix")
            ]),
            .settings("Monitoring", [
                note("hp.src", "Headphone feed", "e.g. cue mix from SSL HP B")
            ])
        ])

    // MOTU 828 MkII --------------------------------------------------------

    static let motu828: DevicePanel = {
        let micStrips: [PanelChannel] = (1...2).map { n -> PanelChannel in
            PanelChannel(key: "mic\(n)", title: "\(n)", subtitle: "MIC·GTR · FRONT", scope: "Mic/Gtr \(n)",
                         inPorts: ["Mic/Gtr \(n) (front)"],
                         controls: [
                            choice("mic\(n).src", "", ["Mic (XLR)", "Guitar (TRS)"]),
                            toggle("mic\(n)", "p48", "48V", Theme.red),
                            note("mic\(n).gain", "Trim", "e.g. 12 o'clock")
                         ])
        }
        let lineIns: [PanelPortRow] = (1...8).map { n -> PanelPortRow in
            PanelPortRow(title: "IN \(n)", ports: ["Line In \(n)"], side: .to)
        }
        var outs: [PanelPortRow] = [
            PanelPortRow(title: "MAIN L", ports: ["Main Out L"], side: .from),
            PanelPortRow(title: "MAIN R", ports: ["Main Out R"], side: .from)
        ]
        for n in 1...8 {
            outs.append(PanelPortRow(title: "OUT \(n)", ports: ["Line Out \(n)"], side: .from))
        }
        outs.append(PanelPortRow(title: "PHONES", ports: ["Phones"], side: .from))
        outs.append(PanelPortRow(title: "SEND 1", ports: ["Mic Send 1"], side: .from, note: "Rear jack · mic/gtr 1 after the preamp"))
        outs.append(PanelPortRow(title: "SEND 2", ports: ["Mic Send 2"], side: .from, note: "Rear jack · mic/gtr 2 after the preamp"))
        let digital: [PanelPortRow] = [
            PanelPortRow(title: "ADAT IN", ports: ["ADAT In (optical)"], side: .to, note: "8 ch at 44.1/48k, 4 ch at 88.2/96k"),
            PanelPortRow(title: "ADAT OUT", ports: ["ADAT Out (optical)"], side: .from, note: "8 ch at 44.1/48k, 4 ch at 88.2/96k"),
            PanelPortRow(title: "S/PDIF IN", ports: ["S/PDIF In (RCA)"], side: .to),
            PanelPortRow(title: "S/PDIF OUT", ports: ["S/PDIF Out (RCA)"], side: .from),
            PanelPortRow(title: "WCLK IN", ports: ["Word Clock In"], side: .to),
            PanelPortRow(title: "WCLK OUT", ports: ["Word Clock Out"], side: .from),
            PanelPortRow(title: "MIDI IN", ports: ["MIDI In"], side: .to),
            PanelPortRow(title: "MIDI OUT", ports: ["MIDI Out"], side: .from),
            PanelPortRow(title: "FIREWIRE", ports: ["FireWire"], side: .from, note: "Unused while the MOTU is an ADAT expander")
        ]
        let clock: [PanelControl] = [
            choice("clock", "Clock source", ["Internal", "ADAT", "S/PDIF", "Word clock"], remind: true),
            choice("rate", "Sample rate", ["48 kHz", "44.1 kHz", "88.2 kHz", "96 kHz"], remind: true),
            choice("optical", "Optical port", ["ADAT", "TOSLink (S/PDIF)"])
        ]
        return DevicePanel(
            deviceID: "motu828",
            resetTitle: "Current settings (front panel / CueMix)",
            resetNote: "The 828 MkII is clocked from the SSL 18 over ADAT — check the clock source if channels drop out or click.",
            sections: [
                .strips("Mic / guitar inputs", micStrips, 200),
                .ports("Line inputs", lineIns),
                .ports("Outputs", outs),
                .ports("Digital, sync & computer", digital),
                .settings("Clock & optical", clock)
            ])
    }()

    // Focusrite VoiceMaster Pro --------------------------------------------

    static let voiceMaster = DevicePanel(
        deviceID: "voicemaster",
        resetTitle: "Current settings to set on the front panel",
        resetNote: "Anything not listed is switched out. Order switches decide where Vintage Harmonics and EQ sit around the compressor.",
        sections: [
            .strips("Input & processing", [
                PanelChannel(key: "in", title: "INPUT", subtitle: "CLASS A PRE", scope: "Input",
                             inPorts: ["Mic In (rear XLR)", "Mic In (front XLR)", "Line In (TRS)", "Inst In (front)"],
                             controls: [
                                choice("in.src", "", ["Mic", "Line", "Inst"]),
                                toggle("in", "p48", "48V", Theme.red),
                                toggle("in", "ph", "Ø", Theme.blue),
                                toggle("in", "ins", "INSERT", Theme.violet),
                                note("in.gain", "Gain", "e.g. 40 dB")
                             ]),
                PanelChannel(key: "fx", title: "PROCESS", subtitle: "SECTIONS IN", scope: "Processing",
                             inPorts: [],
                             controls: [
                                toggle("fx", "exp", "EXPANDER", Theme.green),
                                toggle("fx", "vh", "HARMONICS", Theme.amber),
                                toggle("fx", "comp", "COMP", Theme.white),
                                toggle("fx", "tube", "TUBE", Theme.amber),
                                toggle("fx", "eq", "EQ", Theme.blue),
                                toggle("fx", "dess", "DE-ESS", Theme.violet),
                                choice("fx.ratio", "Compressor ratio", ["Soft 2.5:1", "Hard 6:1"]),
                                choice("fx.order1", "Harmonics / comp order", ["Harmonics first", "Comp first"]),
                                choice("fx.order2", "Comp / EQ order", ["Comp first", "EQ first"])
                             ],
                             emptyText: "")
            ], 260),
            .ports("Outputs", [
                PanelPortRow(title: "MAIN XLR", ports: ["Main Out (XLR +4)"], side: .from, note: "Balanced, +4 dBu"),
                PanelPortRow(title: "MAIN JACK", ports: ["Main Out (jack -10)"], side: .from, note: "Unbalanced, −10 dBV"),
                PanelPortRow(title: "PRE DE-ESS", ports: ["Pre De-esser Out (XLR)"], side: .from, note: "XLR, before the de-esser"),
                PanelPortRow(title: "HEADPHONES", ports: ["Headphones"], side: .from)
            ]),
            .ports("Insert, FX & monitor", [
                PanelPortRow(title: "INS SEND", ports: ["Insert Send"], side: .from, note: "After the preamp, before the expander"),
                PanelPortRow(title: "INS RETURN", ports: ["Insert Return"], side: .to),
                PanelPortRow(title: "FX SEND", ports: ["FX Send"], side: .from, note: "Mono, unbalanced"),
                PanelPortRow(title: "FX RETURN", ports: ["FX Return L", "FX Return R"], side: .to, note: "Stereo"),
                PanelPortRow(title: "MON IN", ports: ["Monitor In L", "Monitor In R"], side: .to, note: "Stereo, for latency-free monitoring"),
                PanelPortRow(title: "MON OUT", ports: ["Monitor Out L", "Monitor Out R"], side: .from, note: "Stereo")
            ])
        ])
}

// MARK: - Things derived from panels

extension StudioState {
    func describe(_ control: PanelControl, value v: String) -> String {
        switch control.kind {
        case .toggle: return "\(control.label) \(v == "1" ? "on" : "off")"
        case .choice: return control.label.isEmpty ? v : "\(control.label): \(v)"
        case .text: return "\(control.label): \(v)"
        }
    }

    func checklist(for panel: DevicePanel) -> [CheckItem] {
        var items: [CheckItem] = []
        for (scope, c) in panel.scopedControls {
            let v = value(panel.deviceID, c.key, c.defaultValue)
            let differs = v != c.defaultValue
            if c.isText && v.trimmingCharacters(in: .whitespaces).isEmpty { continue }
            guard differs || c.alwaysRemind else { continue }
            let what = describe(c, value: v)
            items.append(CheckItem(id: "\(panel.deviceID):\(c.key)", text: scope.isEmpty ? what : "\(scope) → \(what)"))
        }
        return items
    }

    func patchSheet() -> String {
        var lines: [String] = []
        let df = DateFormatter()
        df.dateStyle = .long
        lines.append("ONE BRIGHT PEARL STUDIO — PATCH SHEET")
        lines.append(df.string(from: Date()))
        for panel in Panels.all where device(panel.deviceID) != nil {
            lines.append("")
            lines.append(devName(panel.deviceID).uppercased())
            for ch in panel.channels {
                let flags = ch.controls.compactMap { c -> String? in
                    let v = value(panel.deviceID, c.key, c.defaultValue)
                    switch c.kind {
                    case .toggle: return v == "1" ? c.label : nil
                    case .choice: return (v != c.defaultValue || c.label.isEmpty) ? v : nil
                    case .text: return v.isEmpty ? nil : v
                    }
                }.joined(separator: " ")
                let src = conns(into: panel.deviceID, ids: ch.inPorts.map(pid)).map { endText($0.from) }.joined(separator: ", ")
                let label = value(panel.deviceID, ch.labelKey, "")
                lines.append("  \(ch.scope.padding(toLength: 11, withPad: " ", startingAt: 0)) \(flags.padding(toLength: 24, withPad: " ", startingAt: 0)) \(src.isEmpty ? "—" : "← " + src)\(label.isEmpty ? "" : "  [\(label)]")")
            }
            for s in panel.sections {
                if case .settings(_, let controls) = s {
                    for c in controls {
                        let v = value(panel.deviceID, c.key, c.defaultValue)
                        if !v.isEmpty { lines.append("  \(describe(c, value: v))") }
                    }
                }
            }
            let notes = value(panel.deviceID, "notes", "")
            if !notes.isEmpty { lines.append("  Notes: \(notes)") }
        }
        lines.append("")
        lines.append("CONNECTIONS")
        let grouped = Dictionary(grouping: conns) { devName($0.from.dev) }
        for name in grouped.keys.sorted() {
            lines.append("  " + name.uppercased())
            for c in grouped[name] ?? [] {
                var l = "    \((c.from.port.isEmpty ? "?" : c.from.port).padding(toLength: 18, withPad: " ", startingAt: 0)) → \(endText(c.to))"
                if !c.cable.isEmpty { l += "  (\(c.cable))" }
                if c.isPlanned { l += "  [PLANNED]" }
                if !c.notes.isEmpty { l += "  — \(c.notes)" }
                lines.append(l)
            }
        }
        return lines.joined(separator: "\n")
    }

    /// How a device's current panel settings differ from saved ones (a settings snapshot).
    func settingsDiff(_ dev: String, against other: PanelValues) -> [DiffLine] {
        guard let panel = Panels.panel(for: dev) else { return [] }
        var out: [DiffLine] = []
        for (scope, c) in panel.scopedControls {
            let now = value(dev, c.key, c.defaultValue)
            let then = other[c.key] ?? c.defaultValue
            if now != then {
                let what = c.label.isEmpty ? "mode" : c.label
                let fn = c.isToggle ? (now == "1" ? "on" : "off") : (now.isEmpty ? "—" : now)
                let ft = c.isToggle ? (then == "1" ? "on" : "off") : (then.isEmpty ? "—" : then)
                out.append(DiffLine(kind: .change, text: "~ \(scope.isEmpty ? "" : scope + " ")\(what): now \(fn), snapshot \(ft)"))
            }
        }
        return out
    }

    /// What loading `snap` would change compared with the current state.
    func diff(to snap: Snapshot) -> [DiffLine] {
        func key(_ c: Conn) -> String {
            "\(c.from.dev)|\(c.from.port)|\(c.to.dev)|\(c.to.port)|\(c.status)".lowercased()
        }
        func text(_ c: Conn) -> String {
            "\(endText(c.from)) → \(endText(c.to))\(c.isPlanned ? " (planned)" : "")"
        }
        var out: [DiffLine] = []
        let mine = Set(conns.map(key))
        let theirs = Set(snap.conns.map(key))
        for c in snap.conns where !mine.contains(key(c)) {
            out.append(DiffLine(kind: .add, text: "+ " + text(c)))
        }
        for c in conns where !theirs.contains(key(c)) {
            out.append(DiffLine(kind: .remove, text: "− " + text(c)))
        }
        for panel in Panels.all {
            let dev = panel.deviceID
            let devLabel = devName(dev)
            for (scope, c) in panel.scopedControls {
                let a = value(dev, c.key, c.defaultValue)
                let b = snap.settings[dev]?[c.key] ?? c.defaultValue
                if a != b {
                    let prefix = scope.isEmpty ? devLabel : "\(devLabel) \(scope)"
                    let what = c.label.isEmpty ? "mode" : c.label
                    let fa = c.isToggle ? (a == "1" ? "on" : "off") : (a.isEmpty ? "—" : a)
                    let fb = c.isToggle ? (b == "1" ? "on" : "off") : (b.isEmpty ? "—" : b)
                    out.append(DiffLine(kind: .change, text: "~ \(prefix) \(what): \(fa) → \(fb)"))
                }
            }
            for ch in panel.channels {
                let a = value(dev, ch.labelKey, "")
                let b = snap.settings[dev]?[ch.labelKey] ?? ""
                if a != b {
                    out.append(DiffLine(kind: .change, text: "~ \(devLabel) \(ch.scope) label: \(a.isEmpty ? "—" : a) → \(b.isEmpty ? "—" : b)"))
                }
            }
        }
        return out
    }
}
