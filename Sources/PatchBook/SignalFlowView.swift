import SwiftUI

// MARK: - Automatic layout

typealias FlowEdge = (from: String, to: String)

extension StudioState {
    /// Places devices in columns by signal order: sources on the left, each hop one column to the right.
    /// Separate setups (e.g. the hi-fi corner) are stacked below the main one.
    func arranged(ids: [String]) -> [String: NodePos] {
        let set = Set(ids)
        var seenPairs = Set<String>()
        var edges: [FlowEdge] = conns
            .filter { set.contains($0.from.dev) && set.contains($0.to.dev) && $0.from.dev != $0.to.dev }
            .map { (from: $0.from.dev, to: $0.to.dev) }
        edges = edges.filter { seenPairs.insert($0.from + ">" + $0.to).inserted }
        // Two-way links (insert send/return, ADAT both ways): the smaller box goes on the feeding side.
        var degree: [String: Int] = [:]
        for e in edges {
            degree[e.from, default: 0] += 1
            degree[e.to, default: 0] += 1
        }
        let pairSet = Set(edges.map { $0.from + ">" + $0.to })
        edges = edges.filter { e in
            guard pairSet.contains(e.to + ">" + e.from) else { return true }
            let df = degree[e.from] ?? 0, dt = degree[e.to] ?? 0
            if df != dt { return df < dt }
            return e.from < e.to
        }
        // Split into separate setups.
        var adj: [String: [String]] = [:]
        for e in edges {
            adj[e.from, default: []].append(e.to)
            adj[e.to, default: []].append(e.from)
        }
        let order = ids.sorted { devName($0).localizedStandardCompare(devName($1)) == .orderedAscending }
        var seen = Set<String>()
        var groups: [[String]] = []
        for id in order where !seen.contains(id) {
            var stack = [id]
            seen.insert(id)
            var members: [String] = []
            while let u = stack.popLast() {
                members.append(u)
                for v in adj[u] ?? [] where !seen.contains(v) {
                    seen.insert(v)
                    stack.append(v)
                }
            }
            groups.append(members)
        }
        let multi = groups.filter { $0.count > 1 }.sorted { $0.count > $1.count }
        let singles = groups.filter { $0.count == 1 }.map { $0[0] }
        var result: [String: NodePos] = [:]
        var top = 0.0
        for g in multi {
            let gs = Set(g)
            let layout = layoutGroup(g, edges: edges.filter { gs.contains($0.from) })
            for (id, cell) in layout.cells {
                result[id] = NodePos(x: 140 + Double(cell.col) * 250, y: 80 + top + Double(cell.row) * 104)
            }
            top += Double(layout.rows) * 104 + 50
        }
        for (i, id) in singles.enumerated() {
            result[id] = NodePos(x: 140 + Double(i % 6) * 250, y: 80 + top + Double(i / 6) * 104)
        }
        return result
    }

    func layoutGroup(_ members: [String], edges: [FlowEdge]) -> (cells: [String: (col: Int, row: Int)], rows: Int) {
        let order = members.sorted { devName($0).localizedStandardCompare(devName($1)) == .orderedAscending }
        var outgoing: [String: [String]] = [:]
        var incoming: [String: Int] = [:]
        for e in edges {
            outgoing[e.from, default: []].append(e.to)
            incoming[e.to, default: 0] += 1
        }
        // Drop edges that loop back upstream, then column = longest chain leading in.
        var visit: [String: Int] = [:]
        var preds: [String: [String]] = [:]
        func walk(_ u: String) {
            visit[u] = 1
            for v in outgoing[u] ?? [] {
                switch visit[v] ?? 0 {
                case 0:
                    preds[v, default: []].append(u)
                    walk(v)
                case 2:
                    preds[v, default: []].append(u)
                default:
                    break
                }
            }
            visit[u] = 2
        }
        for id in order where (incoming[id] ?? 0) == 0 { walk(id) }
        while let rest = order.filter({ visit[$0] == nil }).max(by: { (outgoing[$0]?.count ?? 0) < (outgoing[$1]?.count ?? 0) }) {
            walk(rest)
        }
        var depth: [String: Int] = [:]
        func depthOf(_ id: String) -> Int {
            if let d = depth[id] { return d }
            let d = (preds[id] ?? []).map { depthOf($0) + 1 }.max() ?? 0
            depth[id] = d
            return d
        }
        for id in order { _ = depthOf(id) }
        // Pull boxes with no inputs rightwards so they sit just before what they feed
        // (e.g. outboard and the MOTU land in the column right before the SSL).
        var succs: [String: [String]] = [:]
        for (v, ps) in preds { for u in ps { succs[u, default: []].append(v) } }
        var changed = true
        var rounds = 0
        while changed && rounds < 50 {
            changed = false
            rounds += 1
            for id in order where (preds[id] ?? []).isEmpty {
                if let m = (succs[id] ?? []).compactMap({ depth[$0] }).min(), m - 1 > (depth[id] ?? 0) {
                    depth[id] = m - 1
                    changed = true
                }
            }
        }
        let minDepth = depth.values.min() ?? 0
        for id in order { depth[id] = (depth[id] ?? 0) - minDepth }
        let maxDepth = depth.values.max() ?? 0
        var columns: [[String]] = (0...maxDepth).map { c in order.filter { depth[$0] == c } }
        // Order rows so boxes sit level with what feeds them (two passes).
        var rowOf: [String: Double] = [:]
        func avg(_ xs: [Double]) -> Double? { xs.isEmpty ? nil : xs.reduce(0, +) / Double(xs.count) }
        func sortColumn(_ c: Int, _ key: (String) -> Double?) {
            columns[c].sort { a, b in
                let ka = key(a) ?? 999, kb = key(b) ?? 999
                if ka != kb { return ka < kb }
                return (order.firstIndex(of: a) ?? 0) < (order.firstIndex(of: b) ?? 0)
            }
            for (r, id) in columns[c].enumerated() { rowOf[id] = Double(r) }
        }
        let fromRows: (String) -> Double? = { id in avg(edges.filter { $0.to == id }.compactMap { rowOf[$0.from] }) }
        let toRows: (String) -> Double? = { id in avg(edges.filter { $0.from == id }.compactMap { rowOf[$0.to] }) }
        for c in 0...maxDepth { sortColumn(c, fromRows) }
        sortColumn(0, toRows)
        if maxDepth >= 1 {
            for c in 1...maxDepth { sortColumn(c, fromRows) }
        }
        var cells: [String: (col: Int, row: Int)] = [:]
        for (c, col) in columns.enumerated() {
            for (r, id) in col.enumerated() { cells[id] = (col: c, row: r) }
        }
        return (cells: cells, rows: columns.map { $0.count }.max() ?? 1)
    }
}

// MARK: - Canvas

struct WireDrag {
    let from: String
    var point: CGPoint
}

struct CableDrawing {
    let conn: Conn
    let a: CGPoint
    let b: CGPoint
    let fromLabel: String
    let toLabel: String
}

struct SignalFlowView: View {
    @EnvironmentObject var store: Store
    let onOpenPanel: (String) -> Void
    let onShowConnections: (String) -> Void
    let onOpenDevice: (String) -> Void

    @State var live: [String: CGPoint] = [:]       // positions while dragging
    @State var dragStart: [String: CGPoint] = [:]
    @State var wire: WireDrag?
    @State var newConn: ConnEditTarget?
    @State var selected: String?
    @State var showPorts: Bool = false

    static let nodeW: CGFloat = 184
    static let nodeH: CGFloat = 72

    var placed: [Device] {
        store.state.devices.filter { $0.onCanvasAllowed && $0.canvas != nil }
    }

    func pos(_ id: String) -> CGPoint? {
        if let p = live[id] { return p }
        if let n = store.state.device(id)?.canvas { return CGPoint(x: n.x, y: n.y) }
        return nil
    }

    var canvasSize: CGSize {
        var w: CGFloat = 1500, h: CGFloat = 950
        for d in placed {
            if let p = pos(d.id) {
                w = max(w, p.x + 320)
                h = max(h, p.y + 200)
            }
        }
        return CGSize(width: w, height: h)
    }

    var body: some View {
        HStack(spacing: 0) {
            tray
                .frame(width: 220)
            Divider().overlay(Color.black)
            VStack(spacing: 0) {
                toolbar
                Divider().overlay(Theme.line)
                ScrollView([.horizontal, .vertical]) {
                    canvas
                }
                .background(Theme.bg)
            }
        }
        .sheet(item: $newConn) { t in
            ConnEditor(conn: t.conn, isNew: t.isNew)
                .environmentObject(store)
        }
    }

    // MARK: Toolbar & tray

    var toolbar: some View {
        HStack(spacing: 14) {
            Text("Drag boxes to arrange · drag from ● to another box to patch · double-click a box for its device page")
                .font(.system(size: 12))
                .foregroundColor(Theme.mute)
            Spacer()
            legend(Theme.ok, "Connected", dash: false)
            legend(Theme.blue, "Planned", dash: true)
            legend(Theme.brass, "Mic'd", dash: true)
            legend(Theme.amber, "Port ?", dash: false)
            Toggle("All port names", isOn: $showPorts)
                .toggleStyle(.checkbox)
            Button("Arrange by signal flow") { store.arrangeFlow() }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
    }

    func legend(_ color: Color, _ text: String, dash: Bool) -> some View {
        HStack(spacing: 5) {
            Path { p in
                p.move(to: CGPoint(x: 0, y: 5))
                p.addLine(to: CGPoint(x: 22, y: 5))
            }
            .stroke(color, style: StrokeStyle(lineWidth: 2, lineCap: .round, dash: dash ? [4, 4] : []))
            .frame(width: 22, height: 10)
            Text(text).font(.system(size: 11)).foregroundColor(Theme.mute)
        }
    }

    var tray: some View {
        let unplaced = store.state.devices.filter { $0.onCanvasAllowed && $0.canvas == nil }
        return VStack(alignment: .leading, spacing: 0) {
            Text("NOT ON THE CANVAS")
                .font(.system(size: 10, weight: .semibold))
                .tracking(1.2)
                .foregroundColor(Theme.mute)
                .padding(.horizontal, 14)
                .padding(.top, 14)
                .padding(.bottom, 8)
            ScrollView {
                VStack(alignment: .leading, spacing: 4) {
                    if unplaced.isEmpty {
                        Text("Everything is placed.")
                            .font(.system(size: 12))
                            .foregroundColor(Theme.dim)
                    }
                    ForEach(unplaced) { d in
                        Button {
                            addToCanvas(d.id)
                        } label: {
                            HStack(spacing: 8) {
                                Image(systemName: "plus.circle")
                                    .foregroundColor(Theme.green)
                                VStack(alignment: .leading, spacing: 1) {
                                    Text(d.name).font(.system(size: 12, weight: .medium))
                                    Text(d.category.label.uppercased())
                                        .font(.system(size: 8, weight: .semibold))
                                        .tracking(0.8)
                                        .foregroundColor(Theme.dim)
                                }
                                Spacer(minLength: 0)
                            }
                            .padding(.horizontal, 8)
                            .padding(.vertical, 6)
                            .background(RoundedRectangle(cornerRadius: 7).fill(Theme.panel))
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.horizontal, 10)
                .padding(.bottom, 14)
            }
            Text("Patchable gear with jacks shows up here. Guitars and other non-patchable gear stay off until you switch Patchable on in the Inventory.")
                .font(.system(size: 11))
                .foregroundColor(Theme.dim)
                .padding(12)
        }
        .background(Color(hex: 0x171918))
    }

    func addToCanvas(_ id: String) {
        // Drop it in the first free spot in the left column area.
        var y: CGFloat = 80
        let x: CGFloat = 140
        while placed.contains(where: { d in
            guard let p = pos(d.id) else { return false }
            return abs(p.x - x) < SignalFlowView.nodeW && abs(p.y - y) < SignalFlowView.nodeH + 10
        }) {
            y += 104
        }
        store.place(id, at: CGPoint(x: x, y: y))
        selected = id
    }

    // MARK: Canvas

    func cables() -> [CableDrawing] {
        let s = store.state
        let visible = s.conns.filter { pos($0.from.dev) != nil && pos($0.to.dev) != nil && $0.from.dev != $0.to.dev }
        // Spread each box's cables along its edge, ordered by where the other end sits, so they don't bunch up.
        func offsets(_ list: [Conn], other: (Conn) -> String) -> [String: CGFloat] {
            let sorted = list.sorted { (pos(other($0))?.y ?? 0) < (pos(other($1))?.y ?? 0) }
            let n = sorted.count
            let step = n > 1 ? min(12, (SignalFlowView.nodeH - 16) / CGFloat(n - 1)) : 0
            var out: [String: CGFloat] = [:]
            for (i, c) in sorted.enumerated() { out[c.id] = (CGFloat(i) - CGFloat(n - 1) / 2) * step }
            return out
        }
        var outOff: [String: CGFloat] = [:]
        var inOff: [String: CGFloat] = [:]
        for (_, list) in Dictionary(grouping: visible, by: { $0.from.dev }) {
            outOff.merge(offsets(list, other: { $0.to.dev })) { a, _ in a }
        }
        for (_, list) in Dictionary(grouping: visible, by: { $0.to.dev }) {
            inOff.merge(offsets(list, other: { $0.from.dev })) { a, _ in a }
        }
        return visible.compactMap { c in
            guard let pa = pos(c.from.dev), let pb = pos(c.to.dev) else { return nil }
            let a = CGPoint(x: pa.x + SignalFlowView.nodeW / 2, y: pa.y + (outOff[c.id] ?? 0))
            let b = CGPoint(x: pb.x - SignalFlowView.nodeW / 2, y: pb.y + (inOff[c.id] ?? 0))
            let fl = c.isMicd ? "mic'd" : s.portName(c.from)
            let tl = c.isMicd ? "" : s.portName(c.to)
            return CableDrawing(conn: c, a: a, b: b, fromLabel: fl, toLabel: tl)
        }
    }

    static func cablePath(_ a: CGPoint, _ b: CGPoint) -> Path {
        var p = Path()
        p.move(to: a)
        let forward = b.x > a.x + 40
        let dx = forward ? max(50, (b.x - a.x) * 0.45) : 120
        p.addCurve(to: b, control1: CGPoint(x: a.x + dx, y: a.y), control2: CGPoint(x: b.x - dx, y: b.y))
        return p
    }

    static func style(_ c: Conn) -> (Color, StrokeStyle) {
        if c.isMicd {
            return (Theme.brass, StrokeStyle(lineWidth: 2, lineCap: .round, dash: [2, 5]))
        }
        if c.isPlanned {
            return (Theme.blue, StrokeStyle(lineWidth: 2, lineCap: .round, dash: [7, 5]))
        }
        if StudioState.hasUnknownPort(c) {
            return (Theme.amber, StrokeStyle(lineWidth: 2, lineCap: .round))
        }
        return (Theme.ok, StrokeStyle(lineWidth: 2, lineCap: .round))
    }

    var canvas: some View {
        let size = canvasSize
        let drawings = cables()
        let sel = selected
        let ports = showPorts
        let wireNow = wire
        let wireStart: CGPoint? = wireNow.flatMap { w in pos(w.from).map { CGPoint(x: $0.x + SignalFlowView.nodeW / 2, y: $0.y) } }
        return ZStack(alignment: .topLeading) {
            Color.clear
                .frame(width: size.width, height: size.height)
                .contentShape(Rectangle())
                .onTapGesture { selected = nil }
            Canvas { ctx, canvasSize in
                // dot grid
                var dots = Path()
                var gx: CGFloat = 20
                while gx < canvasSize.width {
                    var gy: CGFloat = 20
                    while gy < canvasSize.height {
                        dots.addEllipse(in: CGRect(x: gx - 0.8, y: gy - 0.8, width: 1.6, height: 1.6))
                        gy += 40
                    }
                    gx += 40
                }
                ctx.fill(dots, with: .color(Color(hex: 0x262A27)))
                // cables
                for d in drawings {
                    let (color, stroke) = SignalFlowView.style(d.conn)
                    let touches = sel == nil || d.conn.from.dev == sel || d.conn.to.dev == sel
                    let path = SignalFlowView.cablePath(d.a, d.b)
                    ctx.stroke(path, with: .color(color.opacity(touches ? 0.95 : 0.18)), style: stroke)
                    if ports || (sel != nil && touches) {
                        ctx.draw(Text(d.fromLabel)
                                    .font(.system(size: 10, design: .monospaced))
                                    .foregroundColor(Theme.mute),
                                 at: CGPoint(x: d.a.x + 8, y: d.a.y - 3), anchor: .bottomLeading)
                        ctx.draw(Text(d.toLabel)
                                    .font(.system(size: 10, design: .monospaced))
                                    .foregroundColor(Theme.mute),
                                 at: CGPoint(x: d.b.x - 8, y: d.b.y - 3), anchor: .bottomTrailing)
                    }
                }
                // cable being dragged out
                if let w = wireNow, let start = wireStart {
                    ctx.stroke(SignalFlowView.cablePath(start, w.point), with: .color(Theme.brass),
                               style: StrokeStyle(lineWidth: 2, lineCap: .round, dash: [5, 4]))
                }
            }
            .frame(width: size.width, height: size.height)
            .allowsHitTesting(false)

            ForEach(placed) { d in
                if let p = pos(d.id) {
                    node(d)
                        .position(p)
                }
            }
        }
        .frame(width: size.width, height: size.height)
        .coordinateSpace(name: "flow")
    }

    func hitTest(_ p: CGPoint) -> String? {
        placed.first { d in
            guard let c = pos(d.id) else { return false }
            return abs(c.x - p.x) <= SignalFlowView.nodeW / 2 && abs(c.y - p.y) <= SignalFlowView.nodeH / 2
        }?.id
    }

    func snap(_ p: CGPoint) -> CGPoint {
        CGPoint(x: (p.x / 10).rounded() * 10, y: (p.y / 10).rounded() * 10)
    }

    // The box is split into small pieces so the compiler can type-check each one quickly.

    func nodeCard(_ d: Device, highlight: Color, lineWidth: CGFloat) -> some View {
        let s = store.state
        let ins = s.conns.filter { $0.to.dev == d.id }.count
        let outs = s.conns.filter { $0.from.dev == d.id }.count
        let hasPanel = Panels.panel(for: d.id) != nil
        let category: String = d.category.label.uppercased()
        let counts: String = "\(ins) in · \(outs) out"
        let shape = RoundedRectangle(cornerRadius: 10)
        let fill = LinearGradient(colors: [Color(hex: 0x252A26), Color(hex: 0x1C1F1D)], startPoint: .top, endPoint: .bottom)
        return VStack(alignment: .leading, spacing: 3) {
            HStack(spacing: 6) {
                Text(d.name)
                    .font(.system(size: 13, weight: .semibold))
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
                Spacer(minLength: 0)
                if hasPanel {
                    Image(systemName: "slider.vertical.3")
                        .font(.system(size: 10))
                        .foregroundColor(Theme.dim)
                }
            }
            Text(category)
                .font(.system(size: 9, weight: .semibold))
                .foregroundColor(Theme.green)
            Text(counts)
                .font(.system(size: 10, design: .monospaced))
                .foregroundColor(Theme.dim)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .frame(width: SignalFlowView.nodeW, height: SignalFlowView.nodeH, alignment: .leading)
        .background(shape.fill(fill))
        .overlay(shape.stroke(highlight, lineWidth: lineWidth))
        .shadow(color: Color.black.opacity(0.35), radius: 6, y: 3)
    }

    var inputSocket: some View {
        Circle()
            .fill(Theme.bg)
            .overlay(Circle().stroke(Theme.green, lineWidth: 2))
            .frame(width: 11, height: 11)
            .offset(x: -6)
            .allowsHitTesting(false)
    }

    func wireGesture(_ d: Device) -> some Gesture {
        DragGesture(minimumDistance: 2, coordinateSpace: .named("flow"))
            .onChanged { v in wire = WireDrag(from: d.id, point: v.location) }
            .onEnded { v in
                if let target = hitTest(v.location), target != d.id {
                    let c = Conn(from: End(dev: d.id, port: ""), to: End(dev: target, port: ""))
                    newConn = ConnEditTarget(conn: c, isNew: true)
                }
                wire = nil
            }
    }

    func outputSocket(_ d: Device) -> some View {
        Circle()
            .fill(Theme.brass)
            .overlay(Circle().stroke(Theme.bg, lineWidth: 2))
            .frame(width: 14, height: 14)
            .offset(x: 7)
            .help("Drag to another box to patch a cable")
            .highPriorityGesture(wireGesture(d))
    }

    func moveGesture(_ d: Device) -> some Gesture {
        DragGesture(minimumDistance: 2, coordinateSpace: .named("flow"))
            .onChanged { v in
                let start: CGPoint = dragStart[d.id] ?? pos(d.id) ?? v.startLocation
                if dragStart[d.id] == nil { dragStart[d.id] = start }
                let minX: CGFloat = SignalFlowView.nodeW / 2 + 12
                let minY: CGFloat = SignalFlowView.nodeH / 2 + 12
                live[d.id] = CGPoint(x: max(minX, start.x + v.translation.width),
                                     y: max(minY, start.y + v.translation.height))
                selected = d.id
            }
            .onEnded { _ in
                if let p = live[d.id] { store.place(d.id, at: snap(p)) }
                live[d.id] = nil
                dragStart[d.id] = nil
            }
    }

    func node(_ d: Device) -> some View {
        let isSelected = selected == d.id
        let isTarget: Bool = wire != nil && wire?.from != d.id && wire.map { hitTest($0.point) == d.id } == true
        let highlight: Color = isTarget ? Theme.ok : (isSelected ? Theme.brass : Theme.line2)
        let lineWidth: CGFloat = (isSelected || isTarget) ? 2 : 1
        let hasPanel = Panels.panel(for: d.id) != nil
        return nodeCard(d, highlight: highlight, lineWidth: lineWidth)
            .overlay(alignment: .leading) { inputSocket }
            .overlay(alignment: .trailing) { outputSocket(d) }
            .gesture(moveGesture(d))
            .onTapGesture(count: 2) { onOpenDevice(d.id) }
            .onTapGesture { selected = d.id }
            .contextMenu {
                Button("Open device page") { onOpenDevice(d.id) }
                if hasPanel { Button("Open patch tab") { onOpenPanel(d.id) } }
                Button("Show cables") { onShowConnections(d.id) }
                Divider()
                Button("Remove from canvas") {
                    store.unplace(d.id)
                    if selected == d.id { selected = nil }
                }
            }
    }
}
