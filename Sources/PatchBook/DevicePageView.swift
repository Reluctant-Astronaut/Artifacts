import SwiftUI
import AppKit
import UniformTypeIdentifiers

enum DeviceTab: String, CaseIterable, Identifiable {
    case info = "Info", io = "I/O", photos = "Photos", settings = "Settings", care = "Care log"
    var id: String { rawValue }
}

/// Everything about one device: Info, I/O, Photos, Settings, Care log.
struct DevicePageView: View {
    @EnvironmentObject var store: Store
    let deviceID: String
    let onBack: () -> Void
    let onOpenPanel: (String) -> Void
    let onShowOnCanvas: () -> Void
    @State var tab: DeviceTab = .info
    @State var confirmDelete = false

    var dev: Binding<Device> {
        Binding(get: { store.state.device(deviceID) ?? Device(id: deviceID, name: "?") },
                set: { store.update($0) })
    }

    var body: some View {
        if let d = store.state.device(deviceID) {
            VStack(spacing: 0) {
                header(d)
                Picker("", selection: $tab) {
                    ForEach(DeviceTab.allCases) { t in
                        Text(tabTitle(t, d)).tag(t)
                    }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .frame(maxWidth: 560)
                .padding(.bottom, 12)
                Divider().overlay(Theme.line)
                Group {
                    switch tab {
                    case .info: DeviceInfoTab(dev: dev)
                    case .io: DeviceIOTab(deviceID: deviceID)
                    case .photos: DevicePhotosTab(deviceID: deviceID)
                    case .settings: DeviceSettingsTab(deviceID: deviceID, onOpenPanel: onOpenPanel)
                    case .care: DeviceCareTab(deviceID: deviceID)
                    }
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            }
            .confirmationDialog("Delete \(d.name) from the inventory?", isPresented: $confirmDelete) {
                Button("Delete device, its \(store.connectionCount(deviceID)) cables and its photos", role: .destructive) {
                    onBack()
                    store.deleteDevice(deviceID)
                }
            } message: {
                Text("If you sold it, set its status to Sold instead — it then stays in the archive.")
            }
        } else {
            VStack(spacing: 10) {
                Text("This device is no longer in the inventory.").foregroundColor(Theme.mute)
                Button("Back to Inventory", action: onBack)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    func tabTitle(_ t: DeviceTab, _ d: Device) -> String {
        switch t {
        case .io: return d.ports.isEmpty ? "I/O" : "I/O (\(d.ports.filter { !$0.isPair }.count))"
        case .photos: return d.photos.isEmpty ? "Photos" : "Photos (\(d.photos.count))"
        case .settings: return d.snapshots.isEmpty ? "Settings" : "Settings (\(d.snapshots.count))"
        case .care: return d.care.isEmpty ? "Care log" : "Care log (\(d.care.count))"
        default: return t.rawValue
        }
    }

    func header(_ d: Device) -> some View {
        HStack(alignment: .center, spacing: 14) {
            Button(action: onBack) {
                Label("Back", systemImage: "chevron.left")
            }
            .buttonStyle(.borderless)
            Thumb(url: d.frontPhotoURL, width: 56, height: 56, corner: 8)
            VStack(alignment: .leading, spacing: 4) {
                Text(d.name).font(.system(size: 22, weight: .bold))
                HStack(spacing: 8) {
                    if !d.makerModel.isEmpty {
                        Text(d.makerModel).foregroundColor(Theme.mute)
                    }
                    Chip(text: d.category.label.uppercased(), color: Theme.green)
                    Chip(text: d.status.label.uppercased(), color: statusColor(d.status))
                    if let l = d.location { Chip(text: l.label.uppercased()) }
                    if !d.patchable { Chip(text: "NOT PATCHABLE", color: Theme.dim) }
                }
                .font(.system(size: 12))
            }
            Spacer()
            if Panels.panel(for: d.id) != nil {
                Button("Patch tab") { onOpenPanel(d.id) }
            }
            if d.canvas != nil && d.onCanvasAllowed {
                Button("Show on canvas", action: onShowOnCanvas)
            }
            Menu {
                Button("Delete from inventory…", role: .destructive) { confirmDelete = true }
            } label: {
                Image(systemName: "ellipsis.circle")
            }
            .menuStyle(.borderlessButton)
            .fixedSize()
        }
        .padding(.horizontal, 20)
        .padding(.vertical, 14)
    }
}

// MARK: - Info

struct DeviceInfoTab: View {
    @EnvironmentObject var store: Store
    @Binding var dev: Device

    var purchaseKnown: Binding<Bool> {
        Binding(get: { dev.purchaseDate != nil }, set: { dev.purchaseDate = $0 ? Date() : nil })
    }
    var purchaseDate: Binding<Date> {
        Binding(get: { dev.purchaseDate ?? Date() }, set: { dev.purchaseDate = $0 })
    }
    var soldDate: Binding<Date> {
        Binding(get: { dev.soldDate ?? Date() }, set: { dev.soldDate = $0 })
    }

    var body: some View {
        Form {
            Section("Identity") {
                TextField("Name", text: $dev.name)
                TextField("Maker", text: $dev.maker)
                TextField("Model", text: $dev.model)
                Picker("Category", selection: $dev.category) {
                    ForEach(GearCategory.allCases) { Text($0.label).tag($0) }
                }
                TextField("Serial number", text: $dev.serial)
                TextField("Year made", text: $dev.yearMade, prompt: Text("e.g. 1978 or c. 1900–1920"))
            }
            Section("Purchase & value (NOK)") {
                Toggle("Purchase date known", isOn: purchaseKnown)
                if dev.purchaseDate != nil {
                    DatePicker("Purchase date", selection: purchaseDate, displayedComponents: .date)
                }
                TextField("Purchase price", value: $dev.purchasePrice, format: .number, prompt: Text("kr"))
                TextField("Replacement value", value: $dev.replacementValue, format: .number, prompt: Text("kr — what it would cost to replace today"))
                TextField("Where bought", text: $dev.whereBought)
            }
            Section("In the studio") {
                Picker("Location", selection: $dev.location) {
                    Text("—").tag(Location?.none)
                    ForEach(Location.allCases) { Text($0.label).tag(Location?.some($0)) }
                }
                Picker("Status", selection: $dev.status) {
                    ForEach(GearStatus.allCases) { Text($0.label).tag($0) }
                }
                if dev.status == .sold {
                    DatePicker("Sold on", selection: soldDate, displayedComponents: .date)
                    TextField("Sold for", value: $dev.soldPrice, format: .number, prompt: Text("kr"))
                }
                Toggle(isOn: $dev.patchable) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("Patchable")
                        Text(dev.ports.isEmpty
                             ? "Add jacks on the I/O tab to put it on the canvas."
                             : "Patchable gear with jacks can go on the Signal Flow canvas.")
                            .font(.system(size: 11))
                            .foregroundColor(Theme.mute)
                    }
                }
            }
            Section("Notes") {
                TextEditor(text: $dev.notes)
                    .font(.system(size: 13))
                    .frame(minHeight: 90)
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
    }
}

// MARK: - I/O

struct DeviceIOTab: View {
    @EnvironmentObject var store: Store
    let deviceID: String
    @State var showPair = false
    @State var pendingDelete: Jack?

    func portBinding(_ id: String) -> Binding<Jack> {
        Binding(get: {
            store.state.device(deviceID)?.port(id) ?? Jack(id: id, name: "?", direction: .input, connector: .other)
        }, set: { newValue in
            guard var d = store.state.device(deviceID), let i = d.ports.firstIndex(where: { $0.id == id }) else { return }
            d.ports[i] = newValue
            store.update(d)
        })
    }

    var body: some View {
        let d = store.state.device(deviceID) ?? Device()
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Text(d.onCanvasAllowed
                         ? "Shown on the Signal Flow canvas."
                         : (d.patchable ? "Add jacks to put it on the canvas." : "Not patchable — stays off the canvas (switch it on under Info)."))
                        .font(.system(size: 12))
                        .foregroundColor(Theme.mute)
                    Spacer()
                    Button("Add input") { store.addPort(deviceID, direction: .input) }
                    Button("Add output") { store.addPort(deviceID, direction: .output) }
                    Button("Add stereo pair…") { showPair = true }
                        .disabled(d.ports.filter { !$0.isPair }.count < 2)
                }
                portSection("Inputs", d.inputs, d)
                portSection("Outputs", d.outputs, d)
                Text("Renaming a jack is safe: cables and patch tabs follow it. A stereo pair lets one cable cover two jacks (e.g. Out 1 + Out 2).")
                    .font(.system(size: 12))
                    .foregroundColor(Theme.dim)
                    .padding(.top, 6)
            }
            .padding(20)
        }
        .sheet(isPresented: $showPair) {
            PairSheet(device: d) { a, b in store.addPair(deviceID, a, b) }
        }
        .confirmationDialog("Delete this jack?",
                            isPresented: Binding(get: { pendingDelete != nil }, set: { if !$0 { pendingDelete = nil } }),
                            presenting: pendingDelete) { p in
            Button("Delete “\(p.name)”", role: .destructive) { store.deletePort(deviceID, p.id) }
        } message: { p in
            Text("\(store.cablesOn(deviceID, p.id)) cable(s) use it; they'll be marked “?” until you choose another jack.")
        }
    }

    @ViewBuilder
    func portSection(_ title: String, _ ports: [Jack], _ d: Device) -> some View {
        SectionHeader("\(title) · \(ports.filter { !$0.isPair }.count)")
        if ports.isEmpty {
            Text("None").foregroundColor(Theme.dim).font(.system(size: 12))
        }
        VStack(spacing: 4) {
            ForEach(ports) { p in
                HStack(spacing: 10) {
                    if p.isPair {
                        Image(systemName: "link").foregroundColor(Theme.violet).frame(width: 18)
                        Text(p.name).frame(maxWidth: .infinity, alignment: .leading)
                        Text("pair of " + p.members.compactMap { d.port($0)?.name }.joined(separator: " + "))
                            .font(.system(size: 11))
                            .foregroundColor(Theme.dim)
                    } else {
                        Image(systemName: p.direction == .input ? "arrow.down.to.line" : "arrow.up.to.line")
                            .foregroundColor(Theme.green)
                            .frame(width: 18)
                        TextField("Name", text: portBinding(p.id).name)
                            .textFieldStyle(.roundedBorder)
                        Picker("", selection: portBinding(p.id).connector) {
                            ForEach(Connector.allCases) { Text($0.rawValue).tag($0) }
                        }
                        .labelsHidden()
                        .frame(width: 150)
                    }
                    let n = store.cablesOn(deviceID, p.id)
                    Text(n == 0 ? "free" : "\(n) cable\(n == 1 ? "" : "s")")
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundColor(n == 0 ? Theme.dim : Theme.ok)
                        .frame(width: 70, alignment: .trailing)
                    Button {
                        if store.cablesOn(deviceID, p.id) > 0 { pendingDelete = p } else { store.deletePort(deviceID, p.id) }
                    } label: {
                        Image(systemName: "minus.circle")
                    }
                    .buttonStyle(.borderless)
                    .help("Delete jack")
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(RoundedRectangle(cornerRadius: 8).fill(Theme.panel))
            }
        }
    }
}

struct PairSheet: View {
    @Environment(\.dismiss) var dismiss
    let device: Device
    let onAdd: (String, String) -> Void
    @State var direction: PortDirection = .output
    @State var a: String = ""
    @State var b: String = ""

    var body: some View {
        let singles = device.ports.filter { !$0.isPair && $0.direction == direction }
        VStack(alignment: .leading, spacing: 14) {
            Text("New stereo pair").font(.title2.weight(.bold))
            Form {
                Picker("Side", selection: $direction) {
                    Text("Outputs").tag(PortDirection.output)
                    Text("Inputs").tag(PortDirection.input)
                }
                .pickerStyle(.segmented)
                Picker("Left / first", selection: $a) {
                    Text("Choose…").tag("")
                    ForEach(singles) { Text($0.name).tag($0.id) }
                }
                Picker("Right / second", selection: $b) {
                    Text("Choose…").tag("")
                    ForEach(singles) { Text($0.name).tag($0.id) }
                }
            }
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Add pair") {
                    onAdd(a, b)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(a.isEmpty || b.isEmpty || a == b)
            }
        }
        .padding(22)
        .frame(width: 420)
    }
}

// MARK: - Photos

struct DevicePhotosTab: View {
    @EnvironmentObject var store: Store
    let deviceID: String
    @State var dropping = false

    func photoBinding(_ id: String) -> Binding<PhotoRef> {
        Binding(get: {
            store.state.device(deviceID)?.photos.first { $0.id == id } ?? PhotoRef(file: "")
        }, set: { newValue in
            guard var d = store.state.device(deviceID), let i = d.photos.firstIndex(where: { $0.id == id }) else { return }
            d.photos[i] = newValue
            store.update(d)
        })
    }

    var body: some View {
        let d = store.state.device(deviceID) ?? Device()
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                HStack {
                    Text("Front, back, serial plate, receipt — drop image files here or add them.")
                        .font(.system(size: 12))
                        .foregroundColor(Theme.mute)
                    Spacer()
                    Button("Add photos…") {
                        store.addPhotos(deviceID, urls: store.choosePhotos(), kind: .other)
                    }
                }
                if d.photos.isEmpty {
                    VStack(spacing: 8) {
                        Image(systemName: "photo.on.rectangle.angled").font(.system(size: 30))
                        Text("No photos yet")
                    }
                    .foregroundColor(Theme.dim)
                    .frame(maxWidth: .infinity, minHeight: 200)
                    .overlay(RoundedRectangle(cornerRadius: 12)
                        .stroke(dropping ? Theme.green : Theme.line, style: StrokeStyle(lineWidth: 1.5, dash: [6, 5])))
                }
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 220), spacing: 12, alignment: .top)], spacing: 12) {
                    ForEach(d.photos) { p in
                        let url = Store.photoURL(deviceID, p.file)
                        VStack(alignment: .leading, spacing: 6) {
                            Button {
                                NSWorkspace.shared.open(url)
                            } label: {
                                Thumb(url: url, width: 220, height: 165, corner: 8)
                            }
                            .buttonStyle(.plain)
                            .help("Open in Preview")
                            HStack {
                                Picker("", selection: photoBinding(p.id).kind) {
                                    ForEach(PhotoKind.allCases) { Text($0.label).tag($0) }
                                }
                                .labelsHidden()
                                .frame(width: 130)
                                Spacer()
                                Button {
                                    store.deletePhoto(deviceID, p)
                                } label: {
                                    Image(systemName: "trash")
                                }
                                .buttonStyle(.borderless)
                                .help("Delete photo")
                            }
                            TextField("Caption", text: photoBinding(p.id).caption)
                                .textFieldStyle(.roundedBorder)
                                .font(.system(size: 12))
                        }
                        .frame(width: 220)
                    }
                }
            }
            .padding(20)
        }
        .onDrop(of: [.fileURL], isTargeted: $dropping) { providers in
            for provider in providers {
                _ = provider.loadObject(ofClass: URL.self) { url, _ in
                    if let url = url {
                        DispatchQueue.main.async {
                            store.addPhotos(deviceID, urls: [url], kind: .other)
                        }
                    }
                }
            }
            return true
        }
    }
}

// MARK: - Settings snapshots

struct DeviceSettingsTab: View {
    @EnvironmentObject var store: Store
    let deviceID: String
    let onOpenPanel: (String) -> Void
    @State var newTitle: String = ""
    @State var compareID: String?
    @State var pendingRestore: SettingsSnapshot?
    @State var pendingDelete: SettingsSnapshot?
    @State var capturing = false
    @State var message: String?

    func snapBinding(_ id: String) -> Binding<SettingsSnapshot> {
        Binding(get: {
            store.state.device(deviceID)?.snapshots.first { $0.id == id } ?? SettingsSnapshot()
        }, set: { store.updateSnapshot(deviceID, $0) })
    }

    func defaultTitle() -> String {
        let df = DateFormatter()
        df.dateStyle = .medium
        df.timeStyle = .short
        return "Snapshot \(df.string(from: Date()))"
    }

    func capture() {
        capturing = true
        message = nil
        Task { @MainActor in
            do {
                let png = try await WindowCapture.captureSSL360()
                if let file = store.savePNG(deviceID, data: png) {
                    let title = newTitle.isEmpty ? "SSL 360 capture" : newTitle
                    store.takeSnapshot(deviceID, title: title, images: [file])
                    newTitle = ""
                    message = "Captured the SSL 360 window and saved the panel settings with it."
                }
            } catch {
                message = error.localizedDescription + " If macOS asked for Screen Recording permission, allow Patch Book and reopen it."
            }
            capturing = false
        }
    }

    var body: some View {
        let d = store.state.device(deviceID) ?? Device()
        let panel = Panels.panel(for: deviceID)
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                if let panel = panel {
                    let items = store.state.checklist(for: panel)
                    SectionHeader("Current panel settings") {
                        Button("Open patch tab") { onOpenPanel(deviceID) }.controlSize(.small)
                    }
                    Text(items.isEmpty ? "Everything at default." : items.map { $0.text }.joined(separator: "   ·   "))
                        .font(.system(size: 12))
                        .foregroundColor(items.isEmpty ? Theme.dim : Theme.text)
                        .textSelection(.enabled)
                }
                SectionHeader("Take a snapshot")
                HStack {
                    TextField(defaultTitle(), text: $newTitle)
                        .textFieldStyle(.roundedBorder)
                    Button(panel == nil ? "Save note" : "Save current settings") {
                        store.takeSnapshot(deviceID, title: newTitle.isEmpty ? defaultTitle() : newTitle)
                        newTitle = ""
                    }
                    if deviceID == "ssl18" {
                        Button {
                            capture()
                        } label: {
                            Label(capturing ? "Capturing…" : "Capture SSL 360 window", systemImage: "camera.viewfinder")
                        }
                        .disabled(capturing)
                    }
                }
                if let m = message {
                    Text(m).font(.system(size: 12)).foregroundColor(Theme.amber)
                }
                Text(deviceID == "ssl18"
                     ? "Every saved Setup also leaves a snapshot here. If the SSL 18 resets, use Restore to put the switches back in the patch tab, then work through its reset list in SSL 360."
                     : "A snapshot is a dated note\(panel == nil ? "" : " plus this device's panel switches"). Add screenshots or photos of knob positions to it.")
                    .font(.system(size: 12))
                    .foregroundColor(Theme.mute)

                SectionHeader("Snapshots · \(d.snapshots.count)")
                if d.snapshots.isEmpty {
                    Text("None yet.").foregroundColor(Theme.dim)
                }
                ForEach(d.snapshots) { snap in
                    snapshotCard(snap, panel: panel)
                }
            }
            .padding(20)
        }
        .confirmationDialog("Restore these settings?",
                            isPresented: Binding(get: { pendingRestore != nil }, set: { if !$0 { pendingRestore = nil } }),
                            presenting: pendingRestore) { s in
            Button("Restore “\(s.title)”") { store.restoreSnapshot(deviceID, s) }
        } message: { _ in
            Text("The patch tab's switches change to the snapshot's. Then set the unit itself to match using the reset list.")
        }
        .confirmationDialog("Delete this snapshot?",
                            isPresented: Binding(get: { pendingDelete != nil }, set: { if !$0 { pendingDelete = nil } }),
                            presenting: pendingDelete) { s in
            Button("Delete “\(s.title)”", role: .destructive) { store.deleteSnapshot(deviceID, s) }
        }
    }

    func snapshotCard(_ snap: SettingsSnapshot, panel: DevicePanel?) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                Text(snap.date.formatted(date: .abbreviated, time: .shortened))
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundColor(Theme.mute)
                TextField("Title", text: snapBinding(snap.id).title)
                    .textFieldStyle(.plain)
                    .font(.system(size: 14, weight: .semibold))
                Spacer()
                if snap.values != nil && panel != nil {
                    Button(compareID == snap.id ? "Hide" : "Compare with now") {
                        compareID = compareID == snap.id ? nil : snap.id
                    }
                    Button("Restore") { pendingRestore = snap }
                }
                Button {
                    addImages(to: snap)
                } label: {
                    Image(systemName: "photo.badge.plus")
                }
                .buttonStyle(.borderless)
                .help("Add images")
                Button {
                    pendingDelete = snap
                } label: {
                    Image(systemName: "trash")
                }
                .buttonStyle(.borderless)
            }
            TextField("Note", text: snapBinding(snap.id).note, axis: .vertical)
                .textFieldStyle(.roundedBorder)
                .font(.system(size: 12))
                .lineLimit(1...5)
            if !snap.images.isEmpty {
                ScrollView(.horizontal) {
                    HStack(spacing: 8) {
                        ForEach(snap.images, id: \.self) { f in
                            let url = Store.photoURL(deviceID, f)
                            Button {
                                NSWorkspace.shared.open(url)
                            } label: {
                                Thumb(url: url, width: 200, height: 125, corner: 6)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }
            }
            if compareID == snap.id, let values = snap.values {
                let lines = store.state.settingsDiff(deviceID, against: values)
                VStack(alignment: .leading, spacing: 3) {
                    if lines.isEmpty {
                        Text("Same as now.").foregroundColor(Theme.ok)
                    }
                    ForEach(lines) { l in
                        Text(l.text).foregroundColor(Theme.amber)
                    }
                }
                .font(.system(size: 12, design: .monospaced))
                .padding(8)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(RoundedRectangle(cornerRadius: 8).fill(Theme.well))
            }
        }
        .card(padding: 12)
    }

    func addImages(to snap: SettingsSnapshot) {
        let urls = store.choosePhotos()
        var s = snap
        for url in urls {
            if let data = try? Data(contentsOf: url) {
                let file = "\(newID()).\(url.pathExtension.isEmpty ? "png" : url.pathExtension.lowercased())"
                if (try? data.write(to: Store.photoFolder(deviceID).appendingPathComponent(file))) != nil {
                    s.images.append(file)
                }
            }
        }
        store.updateSnapshot(deviceID, s)
    }
}

// MARK: - Care log

func careMeta(_ e: CareEntry) -> String {
    var parts: [String] = []
    if !e.by.isEmpty { parts.append("by \(e.by)") }
    if let c = e.cost { parts.append(nok(c)) }
    return parts.joined(separator: " · ")
}

struct DeviceCareTab: View {
    @EnvironmentObject var store: Store
    let deviceID: String
    @State var draft = CareEntry()
    @State var pendingDelete: CareEntry?

    var body: some View {
        let d = store.state.device(deviceID) ?? Device()
        let totalCost = d.care.compactMap { $0.cost }.reduce(0, +)
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                SectionHeader("New entry")
                VStack(alignment: .leading, spacing: 8) {
                    HStack(spacing: 10) {
                        DatePicker("", selection: $draft.date, displayedComponents: .date)
                            .labelsHidden()
                            .frame(width: 130)
                        Picker("", selection: $draft.kind) {
                            ForEach(CareKind.allCases) { Text($0.label).tag($0) }
                        }
                        .labelsHidden()
                        .frame(width: 130)
                        TextField("By (you, a tech…)", text: $draft.by)
                            .textFieldStyle(.roundedBorder)
                            .frame(width: 170)
                        TextField("Cost (kr)", value: $draft.cost, format: .number)
                            .textFieldStyle(.roundedBorder)
                            .frame(width: 100)
                    }
                    HStack(spacing: 10) {
                        TextField("What was done — e.g. new strings (D'Addario 10–46), recapped PSU, set intonation", text: $draft.text, axis: .vertical)
                            .textFieldStyle(.roundedBorder)
                            .lineLimit(1...4)
                        Button("Add") {
                            store.addCare(deviceID, draft)
                            draft = CareEntry()
                        }
                        .disabled(draft.text.trimmingCharacters(in: .whitespaces).isEmpty)
                    }
                }
                .card(padding: 12)

                SectionHeader("History · \(d.care.count)") {
                    if totalCost > 0 {
                        Text("Spent: \(nok(totalCost))")
                            .font(.system(size: 12, design: .monospaced))
                            .foregroundColor(Theme.mute)
                    }
                }
                if d.care.isEmpty {
                    Text("No entries yet.").foregroundColor(Theme.dim)
                }
                ForEach(d.care) { e in
                    HStack(alignment: .firstTextBaseline, spacing: 12) {
                        Text(e.date.formatted(date: .abbreviated, time: .omitted))
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundColor(Theme.mute)
                            .frame(width: 100, alignment: .leading)
                        Chip(text: e.kind.label.uppercased(), color: Theme.green)
                            .frame(width: 100, alignment: .leading)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(e.text).textSelection(.enabled)
                            if !e.by.isEmpty || e.cost != nil {
                                Text(careMeta(e))
                                    .font(.system(size: 11))
                                    .foregroundColor(Theme.dim)
                            }
                        }
                        Spacer()
                        Button {
                            pendingDelete = e
                        } label: {
                            Image(systemName: "trash")
                        }
                        .buttonStyle(.borderless)
                    }
                    .font(.system(size: 13))
                    .padding(.vertical, 6)
                    Divider().overlay(Theme.line)
                }
            }
            .padding(20)
        }
        .confirmationDialog("Delete this entry?",
                            isPresented: Binding(get: { pendingDelete != nil }, set: { if !$0 { pendingDelete = nil } }),
                            presenting: pendingDelete) { e in
            Button("Delete", role: .destructive) { store.deleteCare(deviceID, e) }
        }
    }
}
