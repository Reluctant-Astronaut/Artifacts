import SwiftUI

struct ConnEditTarget: Identifiable {
    let id = UUID()
    let conn: Conn
    let isNew: Bool
}

struct ConnectionsView: View {
    @EnvironmentObject var store: Store
    @Binding var deviceFilter: String
    @State var query: String = ""
    @State var statusFilter: String = "all"
    @State var editTarget: ConnEditTarget?
    @State var pendingDelete: Conn?

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                TextField("Search devices, ports, notes…", text: $query)
                    .textFieldStyle(.roundedBorder)
                Picker("", selection: $deviceFilter) {
                    Text("All devices").tag("")
                    Divider()
                    ForEach(store.state.devices.filter { !$0.ports.isEmpty }) { d in
                        Text(d.name).tag(d.id)
                    }
                }
                .labelsHidden()
                .frame(width: 210)
                Picker("", selection: $statusFilter) {
                    Text("All").tag("all")
                    Text("Active").tag("active")
                    Text("Planned").tag("planned")
                    Text("Unchecked").tag("unverified")
                    Text("Needs attention").tag("issues")
                }
                .labelsHidden()
                .frame(width: 160)
                Button {
                    editTarget = ConnEditTarget(conn: Conn(from: End(dev: deviceFilter, port: ""), to: End(dev: "ssl18", port: "")), isNew: true)
                } label: {
                    Label("Connection", systemImage: "plus")
                }
                .keyboardShortcut("n", modifiers: .command)
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 12)

            Divider().overlay(Theme.line)

            ScrollView {
                VStack(alignment: .leading, spacing: 6) {
                    if deviceFilter.isEmpty {
                        let list = filtered().sorted { sortKey($0).localizedStandardCompare(sortKey($1)) == .orderedAscending }
                        if list.isEmpty {
                            emptyBox(store.state.conns.isEmpty ? "No connections yet — add your first cable." : "No matches")
                        } else {
                            ForEach(list) { c in row(c) }
                        }
                    } else {
                        deviceSplit
                    }
                }
                .padding(20)
            }
        }
        .sheet(item: $editTarget) { t in
            ConnEditor(conn: t.conn, isNew: t.isNew)
                .environmentObject(store)
        }
        .confirmationDialog("Remove this connection?",
                            isPresented: Binding(get: { pendingDelete != nil }, set: { if !$0 { pendingDelete = nil } }),
                            presenting: pendingDelete) { c in
            Button("Remove", role: .destructive) { store.deleteConn(c) }
        } message: { c in
            Text("\(store.state.endText(c.from)) → \(store.state.endText(c.to))")
        }
    }

    func sortKey(_ c: Conn) -> String {
        store.state.devName(c.from.dev) + " " + store.state.portName(c.from)
    }

    func filtered() -> [Conn] {
        let s = store.state
        let clashes = s.clashKeys()
        let q = query.lowercased().trimmingCharacters(in: .whitespaces)
        return s.conns.filter { c in
            switch statusFilter {
            case "active": if c.isPlanned { return false }
            case "planned": if !c.isPlanned { return false }
            case "unverified": if c.verified { return false }
            case "issues":
                if !clashes.contains(StudioState.inputKey(c)) && !StudioState.hasUnknownPort(c) { return false }
            default: break
            }
            if !q.isEmpty {
                let hay = [s.devName(c.from.dev), s.portName(c.from), s.devName(c.to.dev), s.portName(c.to), c.cable, c.notes]
                    .joined(separator: " ").lowercased()
                if !hay.contains(q) { return false }
            }
            return true
        }
    }

    var deviceSplit: some View {
        let s = store.state
        let list = filtered()
        let d = s.device(deviceFilter)
        let into = list.filter { $0.to.dev == deviceFilter }
            .sorted { s.portName($0.to).localizedStandardCompare(s.portName($1.to)) == .orderedAscending }
        let outOf = list.filter { $0.from.dev == deviceFilter }
            .sorted { s.portName($0.from).localizedStandardCompare(s.portName($1.from)) == .orderedAscending }
        let freeIn = d.map { s.portStatus($0, side: .to).filter { $0.conns.isEmpty && !$0.unlisted }.map { $0.port } } ?? []
        let freeOut = d.map { s.portStatus($0, side: .from).filter { $0.conns.isEmpty && !$0.unlisted }.map { $0.port } } ?? []
        return HStack(alignment: .top, spacing: 16) {
            VStack(alignment: .leading, spacing: 6) {
                SectionHeader("Into \(d?.name ?? "") · \(into.count)")
                if into.isEmpty { emptyBox("Nothing patched in") }
                ForEach(into) { c in row(c) }
                freePorts("Unused inputs", freeIn)
            }
            .frame(maxWidth: .infinity, alignment: .topLeading)
            VStack(alignment: .leading, spacing: 6) {
                SectionHeader("Out of \(d?.name ?? "") · \(outOf.count)")
                if outOf.isEmpty { emptyBox("Nothing patched out") }
                ForEach(outOf) { c in row(c) }
                freePorts("Unused outputs", freeOut)
            }
            .frame(maxWidth: .infinity, alignment: .topLeading)
        }
    }

    @ViewBuilder
    func freePorts(_ title: String, _ ports: [String]) -> some View {
        if !ports.isEmpty {
            Text(title.uppercased())
                .font(.system(size: 11, weight: .semibold))
                .tracking(1)
                .foregroundColor(Theme.mute)
                .padding(.top, 8)
            Text(ports.joined(separator: "  ·  "))
                .font(.system(size: 12, design: .monospaced))
                .foregroundColor(Theme.mute)
                .textSelection(.enabled)
        }
    }

    func emptyBox(_ text: String) -> some View {
        Text(text)
            .foregroundColor(Theme.dim)
            .frame(maxWidth: .infinity)
            .padding(18)
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(Theme.line, style: StrokeStyle(lineWidth: 1, dash: [5, 4])))
    }

    func row(_ c: Conn) -> some View {
        let s = store.state
        let clash = s.clashKeys().contains(StudioState.inputKey(c))
        let unknown = StudioState.hasUnknownPort(c)
        return VStack(alignment: .leading, spacing: 4) {
            HStack(alignment: .center, spacing: 10) {
                endView(c.from)
                    .frame(maxWidth: .infinity, alignment: .leading)
                Image(systemName: "arrow.right")
                    .foregroundColor(Theme.green)
                endView(c.to)
                    .frame(maxWidth: .infinity, alignment: .leading)
                HStack(spacing: 5) {
                    if !c.cable.isEmpty { Chip(text: c.cable) }
                    if c.isPlanned { Chip(text: "PLANNED", color: Theme.blue) }
                    if clash { Chip(text: "INPUT SHARED", color: Theme.amber) }
                    if unknown { Chip(text: "PORT ?", color: Theme.amber) }
                    Button { store.toggleVerified(c) } label: {
                        Chip(text: c.verified ? "✓ CHECKED" : "CHECK", color: c.verified ? Theme.ok : Theme.mute)
                    }
                    .buttonStyle(.plain)
                    .help("Mark as checked at the gear")
                    Button { editTarget = ConnEditTarget(conn: c, isNew: false) } label: {
                        Image(systemName: "pencil")
                    }
                    .buttonStyle(.borderless)
                    .help("Edit")
                    Button { pendingDelete = c } label: {
                        Image(systemName: "xmark")
                    }
                    .buttonStyle(.borderless)
                    .help("Remove")
                }
                .fixedSize()
            }
            if !c.notes.isEmpty {
                Text(c.notes)
                    .font(.system(size: 12))
                    .foregroundColor(Theme.mute)
            }
        }
        .card(padding: 10, dashed: c.isPlanned)
        .opacity(c.isPlanned ? 0.8 : 1)
        .contentShape(Rectangle())
        .onTapGesture(count: 2) { editTarget = ConnEditTarget(conn: c, isNew: false) }
    }

    func endView(_ e: End) -> some View {
        VStack(alignment: .leading, spacing: 1) {
            Text(store.state.devName(e.dev))
                .font(.system(size: 13, weight: .semibold))
            Text(store.state.portName(e))
                .font(.system(size: 12, design: .monospaced))
                .foregroundColor(Theme.brass)
        }
    }
}

struct ConnEditor: View {
    @EnvironmentObject var store: Store
    @Environment(\.dismiss) var dismiss
    @State var conn: Conn
    let isNew: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(isNew ? "New connection" : "Edit connection")
                .font(.title2.weight(.bold))
            HStack(alignment: .top, spacing: 10) {
                EndEditor(title: "FROM (output)", end: $conn.from, isOutput: true, optionalPort: conn.isMicd)
                Button {
                    let f = conn.from
                    conn.from = conn.to
                    conn.to = f
                } label: {
                    Image(systemName: "arrow.left.arrow.right")
                }
                .buttonStyle(.borderless)
                .padding(.top, 34)
                .help("Swap ends")
                EndEditor(title: "TO (input)", end: $conn.to, isOutput: false, optionalPort: conn.isMicd)
            }
            Grid(alignment: .leading, horizontalSpacing: 10, verticalSpacing: 10) {
                GridRow {
                    Text("Cable").foregroundColor(Theme.mute)
                    Picker("", selection: $conn.cable) {
                        Text("—").tag("")
                        ForEach(cableTypes, id: \.self) { t in Text(t == micdLink ? "Mic'd (acoustic link, no jacks)" : t).tag(t) }
                        if !conn.cable.isEmpty && !cableTypes.contains(conn.cable) {
                            Text(conn.cable).tag(conn.cable)
                        }
                    }
                    .labelsHidden()
                }
                GridRow {
                    Text("Status").foregroundColor(Theme.mute)
                    Picker("", selection: $conn.status) {
                        Text("Connected now").tag("active")
                        Text("Planned / not yet").tag("planned")
                    }
                    .labelsHidden()
                    .pickerStyle(.segmented)
                }
                GridRow {
                    Text("Notes").foregroundColor(Theme.mute)
                    TextField("e.g. hard-patched, gain at 2 o'clock", text: $conn.notes)
                        .textFieldStyle(.roundedBorder)
                }
                GridRow {
                    Text("")
                    Toggle("Checked at the gear", isOn: $conn.verified)
                }
            }
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button(isNew ? "Add" : "Save") {
                    store.saveConn(conn, isNew: isNew)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(conn.from.dev.isEmpty || conn.to.dev.isEmpty)
            }
        }
        .padding(22)
        .frame(width: 600)
    }
}

struct EndEditor: View {
    @EnvironmentObject var store: Store
    let title: String
    @Binding var end: End
    let isOutput: Bool
    let optionalPort: Bool

    var body: some View {
        let d = store.state.device(end.dev)
        let ports = isOutput ? (d?.outputs ?? []) : (d?.inputs ?? [])
        VStack(alignment: .leading, spacing: 8) {
            Text(title)
                .font(.system(size: 11, weight: .bold))
                .tracking(1)
                .foregroundColor(Theme.green)
            Picker("", selection: Binding(get: { end.dev }, set: { v in
                end.dev = v
                end.port = ""
            })) {
                Text("Choose device…").tag("")
                ForEach(store.state.devices.filter { !$0.isSold && (!$0.ports.isEmpty || $0.id == end.dev) }) { dev in
                    Text(dev.name).tag(dev.id)
                }
            }
            .labelsHidden()
            Picker("", selection: $end.port) {
                Text(optionalPort ? "No jack (mic'd)" : "Not chosen yet (?)").tag("")
                ForEach(ports) { p in
                    Text("\(p.name)  ·  \(p.connector.rawValue)").tag(p.id)
                }
            }
            .labelsHidden()
            .disabled(d == nil)
            if d != nil && ports.isEmpty {
                Text("This device has no \(isOutput ? "outputs" : "inputs") yet — add them on its device page (I/O).")
                    .font(.system(size: 11))
                    .foregroundColor(Theme.amber)
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color(hex: 0x151816)))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Theme.line, lineWidth: 1))
    }
}
