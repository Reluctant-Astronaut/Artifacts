import SwiftUI

/// Draws any device panel defined in Panels.swift.
struct DevicePanelView: View {
    @EnvironmentObject var store: Store
    let panel: DevicePanel
    let onShowConnections: (String) -> Void
    let onOpenDevice: (String) -> Void

    var dev: String { panel.deviceID }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                header
                ForEach(panel.sections) { section in
                    sectionView(section)
                }
                checklistSection
                SectionHeader("Notes")
                TextEditor(text: textBinding("notes"))
                    .font(.system(size: 13))
                    .scrollContentBackground(.hidden)
                    .padding(6)
                    .frame(minHeight: 80)
                    .background(RoundedRectangle(cornerRadius: 8).fill(Theme.well))
                    .overlay(RoundedRectangle(cornerRadius: 8).stroke(Theme.line2, lineWidth: 1))
            }
            .padding(20)
        }
    }

    var header: some View {
        let d = store.state.device(dev)
        let n = store.state.conns.filter { $0.from.dev == dev || $0.to.dev == dev }.count
        return HStack(alignment: .firstTextBaseline, spacing: 12) {
            Text(d?.name ?? dev)
                .font(.system(size: 24, weight: .bold))
            Text((d?.category.label ?? "").uppercased())
                .font(.system(size: 11, weight: .semibold))
                .tracking(1.4)
                .foregroundColor(Theme.green)
            Spacer()
            Text("\(n) connection\(n == 1 ? "" : "s")")
                .font(.system(size: 12, design: .monospaced))
                .foregroundColor(Theme.mute)
            Button("Device page") { onOpenDevice(dev) }
                .controlSize(.small)
            Button("Show in Connections") { onShowConnections(dev) }
                .controlSize(.small)
        }
    }

    // MARK: Bindings

    func textBinding(_ key: String) -> Binding<String> {
        Binding(get: { store.value(dev, key) },
                set: { store.setValue(dev, key, $0) })
    }

    // MARK: Sections

    @ViewBuilder
    func sectionView(_ section: PanelSection) -> some View {
        switch section {
        case .strips(let title, let channels, let minWidth):
            SectionHeader(title) {
                if title == "Inputs" || title.contains("input") || title.contains("Input") {
                    Text("Sources come from Connections")
                        .font(.system(size: 12))
                        .foregroundColor(Theme.mute)
                }
            }
            LazyVGrid(columns: [GridItem(.adaptive(minimum: minWidth), spacing: 8, alignment: .top)],
                      alignment: .leading, spacing: 8) {
                ForEach(channels) { ch in
                    ChannelStripView(dev: dev, channel: ch)
                }
            }
        case .ports(let title, let rows):
            SectionHeader(title)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 270), spacing: 6)], alignment: .leading, spacing: 6) {
                ForEach(rows) { row in
                    portRow(row)
                }
            }
        case .settings(let title, let controls):
            SectionHeader(title)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 270), spacing: 6, alignment: .top)], alignment: .leading, spacing: 6) {
                ForEach(controls) { c in
                    VStack(alignment: .leading, spacing: 6) {
                        Text(c.label.uppercased())
                            .font(.system(size: 10, weight: .semibold))
                            .tracking(1)
                            .foregroundColor(Theme.mute)
                        ControlView(dev: dev, control: c, scope: "")
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .card(padding: 10)
                }
            }
        }
    }

    func portRow(_ row: PanelPortRow) -> some View {
        let s = store.state
        let conns = row.side == .from ? s.conns(outOf: dev, ids: row.ports.map(pid), includePlanned: true)
                                       : s.conns(into: dev, ids: row.ports.map(pid), includePlanned: true)
        let extra = row.annotate?({ key in s.value(dev, key, "") })
        return HStack(alignment: .top, spacing: 10) {
            VStack(alignment: .leading, spacing: 2) {
                Text(row.title)
                    .font(.system(size: 12, weight: .semibold, design: .monospaced))
                    .foregroundColor(Theme.brass)
                Text(row.side == .from ? "OUT" : "IN")
                    .font(.system(size: 9, weight: .semibold, design: .monospaced))
                    .foregroundColor(Theme.dim)
            }
            .frame(width: 92, alignment: .leading)
            VStack(alignment: .leading, spacing: 2) {
                if let e = extra {
                    Text(e).foregroundColor(Theme.violet)
                }
                if conns.isEmpty && extra == nil {
                    Text("—").foregroundColor(Theme.dim)
                }
                ForEach(conns) { c in
                    HStack(spacing: 5) {
                        Text(row.side == .from ? "→ \(s.endText(c.to))" : "← \(s.endText(c.from))")
                            .foregroundColor(c.isPlanned ? Theme.blue : Theme.text)
                        if c.isPlanned { Chip(text: "PLANNED", color: Theme.blue) }
                    }
                }
                if !row.note.isEmpty {
                    Text(row.note)
                        .font(.system(size: 11))
                        .foregroundColor(Theme.dim)
                }
            }
            .font(.system(size: 13))
            Spacer(minLength: 0)
        }
        .frame(minHeight: 26)
        .card(padding: 9)
    }

    var checklistSection: some View {
        let items = store.state.checklist(for: panel)
        let done = items.filter { store.ticks.contains($0.id) }.count
        return VStack(alignment: .leading, spacing: 8) {
            SectionHeader(panel.resetTitle) {
                Text("\(done)/\(items.count)")
                    .font(.system(size: 12, design: .monospaced))
                    .foregroundColor(Theme.mute)
                Button("Clear ticks") {
                    for i in items { store.ticks.remove(i.id) }
                }
                .controlSize(.small)
            }
            if items.isEmpty {
                Text("Everything is at default.")
                    .foregroundColor(Theme.dim)
            }
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 250), spacing: 6)], alignment: .leading, spacing: 6) {
                ForEach(items) { item in
                    let isDone = store.ticks.contains(item.id)
                    Button {
                        if isDone { store.ticks.remove(item.id) } else { store.ticks.insert(item.id) }
                    } label: {
                        HStack(spacing: 9) {
                            Image(systemName: isDone ? "checkmark.square.fill" : "square")
                                .foregroundColor(isDone ? Theme.ok : Theme.mute)
                            Text(item.text)
                                .strikethrough(isDone)
                                .foregroundColor(isDone ? Theme.mute : Theme.text)
                            Spacer(minLength: 0)
                        }
                        .font(.system(size: 13))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .card(padding: 9)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            }
            Text(panel.resetNote)
                .font(.system(size: 12))
                .foregroundColor(Theme.mute)
        }
    }
}

/// One channel strip: label, what's patched in, then its switches.
struct ChannelStripView: View {
    @EnvironmentObject var store: Store
    let dev: String
    let channel: PanelChannel

    var body: some View {
        let s = store.state
        let sources = s.conns(into: dev, ids: channel.inPorts.map(pid))
        let choices = channel.controls.filter { if case .choice = $0.kind { return true }; return false }
        let toggles = channel.controls.filter { $0.isToggle }
        let texts = channel.controls.filter { $0.isText }
        let p48On = s.value(dev, channel.key + ".p48", "0") == "1"
        let mode = channel.controls.first { $0.label.isEmpty }.map { s.value(dev, $0.key, $0.defaultValue) }
        VStack(alignment: .leading, spacing: 7) {
            HStack(alignment: .firstTextBaseline) {
                Text(channel.title)
                    .font(.system(size: channel.title.count > 3 ? 15 : 20, weight: .semibold, design: .monospaced))
                    .foregroundColor(Theme.brass)
                Spacer()
                Text(channel.subtitle)
                    .font(.system(size: 9, design: .monospaced))
                    .foregroundColor(Theme.dim)
            }
            if !channel.inPorts.isEmpty {
                TextField("Label…", text: Binding(get: { store.value(dev, channel.labelKey) },
                                                  set: { store.setValue(dev, channel.labelKey, $0) }))
                    .textFieldStyle(.roundedBorder)
                    .font(.system(size: 12))
                Text(sources.isEmpty ? channel.emptyText : sources.map { "← " + s.endText($0.from) }.joined(separator: "\n"))
                    .font(.system(size: 11))
                    .foregroundColor(sources.isEmpty ? Theme.dim : Theme.green)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(minHeight: 14, alignment: .topLeading)
            }
            ForEach(choices.filter { $0.label.isEmpty }) { c in
                ControlView(dev: dev, control: c, scope: channel.scope)
            }
            if !toggles.isEmpty {
                LazyVGrid(columns: [GridItem(.flexible(), spacing: 5), GridItem(.flexible(), spacing: 5)], spacing: 5) {
                    ForEach(toggles) { c in
                        ControlView(dev: dev, control: c, scope: channel.scope)
                    }
                }
            }
            ForEach(choices.filter { !$0.label.isEmpty }) { c in
                VStack(alignment: .leading, spacing: 3) {
                    Text(c.label.uppercased())
                        .font(.system(size: 9, weight: .semibold))
                        .tracking(0.8)
                        .foregroundColor(Theme.dim)
                    ControlView(dev: dev, control: c, scope: channel.scope)
                }
            }
            ForEach(texts) { c in
                VStack(alignment: .leading, spacing: 3) {
                    Text(c.label.uppercased())
                        .font(.system(size: 9, weight: .semibold))
                        .tracking(0.8)
                        .foregroundColor(Theme.dim)
                    ControlView(dev: dev, control: c, scope: channel.scope)
                }
            }
            if p48On, let m = mode, !m.hasPrefix("Mic") {
                Text("⚠ 48V is on but the input isn't on Mic")
                    .font(.system(size: 11))
                    .foregroundColor(Theme.amber)
            }
        }
        .card(padding: 9, dashed: channel.dashed)
    }
}

/// A single switch, choice or note field bound to the panel settings.
struct ControlView: View {
    @EnvironmentObject var store: Store
    let dev: String
    let control: PanelControl
    let scope: String

    var current: String { store.value(dev, control.key, control.defaultValue) }

    func logText(_ v: String) -> String {
        let name = store.state.devName(dev)
        let what = store.state.describe(control, value: v)
        return scope.isEmpty ? "\(name): \(what)" : "\(name) \(scope): \(what)"
    }

    var body: some View {
        switch control.kind {
        case .toggle(let color):
            LedButton(control.label, on: current == "1", color: color) {
                let v = current == "1" ? "0" : "1"
                store.setValue(dev, control.key, v, logText: logText(v))
            }
        case .choice(let options):
            if options.count <= 3 && options.joined().count <= 22 {
                ModeSegment(options: options, value: current) { v in
                    store.setValue(dev, control.key, v, logText: logText(v))
                }
            } else {
                Picker("", selection: Binding(get: { current },
                                              set: { v in store.setValue(dev, control.key, v, logText: logText(v)) })) {
                    ForEach(options, id: \.self) { o in Text(o).tag(o) }
                }
                .labelsHidden()
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        case .text(let placeholder):
            TextField(placeholder, text: Binding(get: { store.value(dev, control.key) },
                                                 set: { store.setValue(dev, control.key, $0) }))
                .textFieldStyle(.roundedBorder)
                .font(.system(size: 12))
        }
    }
}
