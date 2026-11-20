import SwiftUI

/// One jack on a device, with whatever is plugged into it.
struct PortStatus: Identifiable {
    let id: String
    let port: String
    let conns: [Conn]
    let unlisted: Bool       // a cable on a jack name that isn't in the device's port list
}

extension StudioState {
    /// The device's jacks on one side, each with its cables. Cables on a stereo pair are listed under both
    /// single jacks; cables with no jack chosen yet are listed at the end as "?".
    func portStatus(_ d: Device, side: Side) -> [PortStatus] {
        let ports = side == .to ? d.inputs : d.outputs
        let singles = ports.filter { !$0.isPair }
        let mine = conns.filter { (side == .to ? $0.to.dev : $0.from.dev) == d.id }
        var rows: [PortStatus] = []
        for p in singles {
            let hits = mine.filter { c in
                let e = side == .to ? c.to : c.from
                return !e.port.isEmpty && d.expand(e.port).contains(p.id)
            }
            rows.append(PortStatus(id: "\(side == .to ? "i" : "o"):\(p.id)", port: p.name, conns: hits, unlisted: false))
        }
        let unknown = mine.filter { c in
            let e = side == .to ? c.to : c.from
            return e.port.isEmpty && !c.isMicd
        }
        if !unknown.isEmpty {
            rows.append(PortStatus(id: "\(side == .to ? "i" : "o"):?", port: "?", conns: unknown, unlisted: true))
        }
        return rows
    }
}

struct OverviewView: View {
    @EnvironmentObject var store: Store
    let onOpenPanel: (String) -> Void
    let onShowConnections: (String) -> Void
    let onOpenDevice: (String) -> Void
    @State var query: String = ""
    @State var hideFree: Bool = false
    @State var category: String = ""

    var body: some View {
        let s = store.state
        let active = s.activeConns.count
        let planned = s.conns.count - active
        let unknown = s.conns.filter { StudioState.hasUnknownPort($0) }.count
        let gear = s.devices.filter { !$0.isSold && !$0.ports.isEmpty }
        let freeIns = gear.reduce(0) { sum, d in sum + s.portStatus(d, side: .to).filter { $0.conns.isEmpty }.count }
        let freeOuts = gear.reduce(0) { sum, d in sum + s.portStatus(d, side: .from).filter { $0.conns.isEmpty }.count }
        let cats = GearCategory.allCases.filter { c in gear.contains { $0.category == c } }
        let q = query.lowercased().trimmingCharacters(in: .whitespaces)
        let shown = gear.filter { d in
            (category.isEmpty || d.category.rawValue == category) &&
            (q.isEmpty || d.name.lowercased().contains(q) || d.ports.contains { $0.name.lowercased().contains(q) })
        }

        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                HStack(spacing: 10) {
                    stat("\(gear.count)", "devices with jacks")
                    stat("\(active)", "cables in use")
                    stat("\(planned)", "planned", color: Theme.blue)
                    stat("\(freeIns)", "free inputs")
                    stat("\(freeOuts)", "free outputs")
                    if unknown > 0 { stat("\(unknown)", "need a port name", color: Theme.amber) }
                }
                HStack(spacing: 8) {
                    TextField("Find a device or jack…", text: $query)
                        .textFieldStyle(.roundedBorder)
                    Picker("", selection: $category) {
                        Text("All categories").tag("")
                        ForEach(cats) { Text($0.label).tag($0.rawValue) }
                    }
                    .labelsHidden()
                    .frame(width: 170)
                    Toggle("Hide free jacks", isOn: $hideFree)
                        .toggleStyle(.checkbox)
                }
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 380), spacing: 10, alignment: .top)],
                          alignment: .leading, spacing: 10) {
                    ForEach(shown) { d in
                        deviceCard(d)
                    }
                }
                if shown.isEmpty {
                    Text("No devices match.")
                        .foregroundColor(Theme.dim)
                        .padding(.top, 20)
                }
                Text("Gear with jacks in the Inventory appears here automatically. Sold gear is left out.")
                    .font(.system(size: 12))
                    .foregroundColor(Theme.mute)
                    .padding(.top, 6)
            }
            .padding(20)
        }
    }

    func stat(_ number: String, _ label: String, color: Color = Theme.brass) -> some View {
        VStack(alignment: .leading, spacing: 1) {
            Text(number)
                .font(.system(size: 20, weight: .semibold, design: .monospaced))
                .foregroundColor(color)
            Text(label.uppercased())
                .font(.system(size: 9, weight: .semibold))
                .tracking(1)
                .foregroundColor(Theme.mute)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(RoundedRectangle(cornerRadius: 9).fill(Theme.panel))
        .overlay(RoundedRectangle(cornerRadius: 9).stroke(Theme.line, lineWidth: 1))
    }

    func deviceCard(_ d: Device) -> some View {
        let s = store.state
        let ins = s.portStatus(d, side: .to).filter { !hideFree || !$0.conns.isEmpty }
        let outs = s.portStatus(d, side: .from).filter { !hideFree || !$0.conns.isEmpty }
        let hasPanel = Panels.panel(for: d.id) != nil
        return VStack(alignment: .leading, spacing: 10) {
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                Text(d.name).font(.system(size: 15, weight: .semibold))
                Text(d.category.label.uppercased())
                    .font(.system(size: 9, weight: .semibold))
                    .tracking(1)
                    .foregroundColor(Theme.green)
                Spacer()
                if hasPanel {
                    Button("Panel") { onOpenPanel(d.id) }.controlSize(.small)
                }
                Button("Cables") { onShowConnections(d.id) }.controlSize(.small)
                Button("Info") { onOpenDevice(d.id) }.controlSize(.small)
            }
            HStack(alignment: .top, spacing: 12) {
                portColumn("IN", ins, side: .to, total: d.inputs.isEmpty)
                portColumn("OUT", outs, side: .from, total: d.outputs.isEmpty)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .card(padding: 12)
    }

    func portColumn(_ title: String, _ rows: [PortStatus], side: Side, total noPorts: Bool) -> some View {
        let s = store.state
        return VStack(alignment: .leading, spacing: 4) {
            Text(title)
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundColor(Theme.dim)
            if rows.isEmpty {
                Text(noPorts ? "none" : "all free")
                    .font(.system(size: 11))
                    .foregroundColor(Theme.dim)
            }
            ForEach(rows) { r in
                let planned = !r.conns.isEmpty && r.conns.allSatisfy { $0.isPlanned }
                let dot: Color = r.conns.isEmpty ? Theme.ledOff : (planned ? Theme.blue : (r.unlisted ? Theme.amber : Theme.ok))
                HStack(alignment: .firstTextBaseline, spacing: 6) {
                    Circle().fill(dot).frame(width: 6, height: 6)
                    VStack(alignment: .leading, spacing: 1) {
                        Text(r.port)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundColor(r.conns.isEmpty ? Theme.dim : (r.unlisted ? Theme.amber : Theme.brass))
                        ForEach(r.conns) { c in
                            Text(side == .to ? "← \(s.endText(c.from))" : "→ \(s.endText(c.to))")
                                .font(.system(size: 11))
                                .foregroundColor(c.isPlanned ? Theme.blue : Theme.text)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .topLeading)
    }
}
