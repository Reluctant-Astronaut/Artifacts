import Foundation
import AppKit
import UniformTypeIdentifiers

/// Holds the studio and keeps it saved in ~/Library/Application Support/PatchBook/:
///   state.json   – every device record, cables, history
///   setups.json  – saved setups
///   Photos/<device id>/…  – photos and settings screenshots
final class Store: ObservableObject {
    @Published var state: StudioState {
        didSet { scheduleSave() }
    }
    @Published var setups: [Setup] = [] {
        didSet { saveSetups() }
    }
    /// Ticks on the after-reset checklists (not saved).
    @Published var ticks: Set<String> = []

    private var saveWork: DispatchWorkItem?

    static var folder: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("PatchBook", isDirectory: true)
        try? FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)
        return base
    }
    static var stateURL: URL { folder.appendingPathComponent("state.json") }
    static var setupsURL: URL { folder.appendingPathComponent("setups.json") }
    static var photosFolder: URL { folder.appendingPathComponent("Photos", isDirectory: true) }

    static func photoFolder(_ deviceID: String) -> URL {
        let url = photosFolder.appendingPathComponent(deviceID, isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    static func photoURL(_ deviceID: String, _ file: String) -> URL {
        photosFolder.appendingPathComponent(deviceID).appendingPathComponent(file)
    }

    init() {
        if let data = try? Data(contentsOf: Store.stateURL),
           let s = try? JSONCoding.decoder.decode(StudioState.self, from: data) {
            state = s
        } else if let data = seedJSON.data(using: .utf8),
                  var s = try? JSONCoding.decoder.decode(StudioState.self, from: data) {
            s.log = [LogEntry(ts: nowMS(), msg: "Patch Book started with the studio’s gear. Anything marked PORT ? needs a jack chosen.")]
            state = s
        } else {
            state = StudioState()
        }
        if !state.devices.contains(where: { $0.canvas != nil }) {
            let ids = state.devices.filter { d in
                d.onCanvasAllowed && state.conns.contains { $0.from.dev == d.id || $0.to.dev == d.id }
            }.map { $0.id }
            let layout = state.arranged(ids: ids)
            for i in state.devices.indices {
                if let p = layout[state.devices[i].id] { state.devices[i].canvas = p }
            }
        }
        if let data = try? Data(contentsOf: Store.setupsURL),
           let l = try? JSONCoding.decoder.decode([Setup].self, from: data) {
            setups = l
        }
        saveNow()
    }

    // MARK: Saving

    private func scheduleSave() {
        saveWork?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.saveNow() }
        saveWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5, execute: work)
    }

    func saveNow() {
        if let data = try? JSONCoding.encoder.encode(state) {
            try? data.write(to: Store.stateURL, options: .atomic)
        }
    }

    private func saveSetups() {
        if let data = try? JSONCoding.encoder.encode(setups) {
            try? data.write(to: Store.setupsURL, options: .atomic)
        }
    }

    func log(_ msg: String) {
        state.log.insert(LogEntry(ts: nowMS(), msg: msg), at: 0)
        if state.log.count > 300 { state.log.removeLast(state.log.count - 300) }
    }

    // MARK: Devices

    func index(_ id: String) -> Int? { state.devices.firstIndex { $0.id == id } }

    /// Replace a device record (used by every edit on the device page).
    func update(_ d: Device) {
        guard let i = index(d.id) else { return }
        let old = state.devices[i]
        var new = d
        if new.status == .sold {
            new.canvas = nil
            if new.soldDate == nil { new.soldDate = Date() }
        }
        if !new.onCanvasAllowed { new.canvas = nil }
        state.devices[i] = new
        if old.status != new.status { log("\(new.name): status → \(new.status.label)") }
    }

    func binding(_ id: String) -> (get: () -> Device, set: (Device) -> Void) {
        (get: { [weak self] in self?.state.device(id) ?? Device(id: id, name: "?") },
         set: { [weak self] d in self?.update(d) })
    }

    @discardableResult
    func addDevice(name: String, category: GearCategory) -> String {
        var d = Device(name: name)
        d.category = category
        d.patchable = ![.instrument, .playback].contains(category)
        state.devices.append(d)
        log("Added to inventory: \(name)")
        return d.id
    }

    func deleteDevice(_ id: String) {
        guard let d = state.device(id) else { return }
        state.devices.removeAll { $0.id == id }
        state.conns.removeAll { $0.from.dev == id || $0.to.dev == id }
        try? FileManager.default.removeItem(at: Store.photosFolder.appendingPathComponent(id))
        log("Deleted from inventory: \(d.name)")
    }

    func connectionCount(_ id: String) -> Int {
        state.conns.filter { $0.from.dev == id || $0.to.dev == id }.count
    }

    // MARK: Device panels

    func value(_ dev: String, _ key: String, _ fallback: String = "") -> String {
        state.value(dev, key, fallback)
    }

    /// Change one panel setting. `logText` goes into History (nil while typing in text fields).
    func setValue(_ dev: String, _ key: String, _ newValue: String, logText: String? = nil) {
        guard let i = index(dev), state.devices[i].settings[key] != newValue else { return }
        state.devices[i].settings[key] = newValue
        if let t = logText { log(t) }
    }

    // MARK: Signal flow canvas

    func place(_ id: String, at p: CGPoint) {
        guard let i = index(id) else { return }
        state.devices[i].canvas = NodePos(x: Double(p.x), y: Double(p.y))
    }

    func unplace(_ id: String) {
        guard let i = index(id) else { return }
        state.devices[i].canvas = nil
    }

    func arrangeFlow() {
        let ids = state.devices.filter { $0.canvas != nil }.map { $0.id }
        let layout = state.arranged(ids: ids)
        for i in state.devices.indices {
            if let p = layout[state.devices[i].id] { state.devices[i].canvas = p }
        }
        log("Arranged the signal flow canvas")
    }

    // MARK: Jacks

    func addPort(_ dev: String, direction: PortDirection) {
        guard let i = index(dev) else { return }
        let base = direction == .input ? "In" : "Out"
        var n = state.devices[i].ports.filter { $0.direction == direction }.count + 1
        var id = pid("\(base) \(n)")
        while state.devices[i].port(id) != nil {
            n += 1
            id = pid("\(base) \(n)")
        }
        state.devices[i].ports.append(Jack(id: id + "-" + newID().prefix(3), name: "\(base) \(n)", direction: direction, connector: .trs))
    }

    func addPair(_ dev: String, _ a: String, _ b: String) {
        guard let i = index(dev), let pa = state.devices[i].port(a), let pb = state.devices[i].port(b) else { return }
        let name = "\(pa.name) + \(pb.name)"
        state.devices[i].ports.append(Jack(id: "pair-" + newID(), name: name, direction: pa.direction,
                                           connector: pa.connector, members: [a, b]))
    }

    func deletePort(_ dev: String, _ portID: String) {
        guard let i = index(dev) else { return }
        state.devices[i].ports.removeAll { $0.id == portID || $0.members.contains(portID) }
        for c in state.conns.indices {
            if state.conns[c].from.dev == dev && state.conns[c].from.port == portID { state.conns[c].from.port = "" }
            if state.conns[c].to.dev == dev && state.conns[c].to.port == portID { state.conns[c].to.port = "" }
        }
    }

    func cablesOn(_ dev: String, _ portID: String) -> Int {
        state.conns.filter { ($0.from.dev == dev && $0.from.port == portID) || ($0.to.dev == dev && $0.to.port == portID) }.count
    }

    // MARK: Photos

    /// Copies image files into the device's photo folder.
    func addPhotos(_ dev: String, urls: [URL], kind: PhotoKind) {
        guard let i = index(dev) else { return }
        let folder = Store.photoFolder(dev)
        for url in urls {
            let ext = url.pathExtension.isEmpty ? "jpg" : url.pathExtension.lowercased()
            let file = "\(newID()).\(ext)"
            do {
                try FileManager.default.copyItem(at: url, to: folder.appendingPathComponent(file))
                let k: PhotoKind = state.devices[i].photos.isEmpty ? .front : kind
                state.devices[i].photos.append(PhotoRef(kind: k, file: file))
            } catch {
                continue
            }
        }
    }

    func savePNG(_ dev: String, data: Data) -> String? {
        let file = "\(newID()).png"
        do {
            try data.write(to: Store.photoFolder(dev).appendingPathComponent(file))
            return file
        } catch {
            return nil
        }
    }

    func deletePhoto(_ dev: String, _ photo: PhotoRef) {
        guard let i = index(dev) else { return }
        state.devices[i].photos.removeAll { $0.id == photo.id }
        try? FileManager.default.removeItem(at: Store.photoURL(dev, photo.file))
    }

    func choosePhotos() -> [URL] {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.image]
        panel.allowsMultipleSelection = true
        return panel.runModal() == .OK ? panel.urls : []
    }

    // MARK: Settings snapshots

    @discardableResult
    func takeSnapshot(_ dev: String, title: String, note: String = "", images: [String] = []) -> String? {
        guard let i = index(dev) else { return nil }
        let hasPanel = Panels.panel(for: dev) != nil
        let snap = SettingsSnapshot(title: title, note: note,
                                    values: hasPanel ? state.devices[i].settings : nil, images: images)
        state.devices[i].snapshots.insert(snap, at: 0)
        log("\(state.devices[i].name): settings snapshot “\(title)”")
        return snap.id
    }

    func updateSnapshot(_ dev: String, _ snap: SettingsSnapshot) {
        guard let i = index(dev), let s = state.devices[i].snapshots.firstIndex(where: { $0.id == snap.id }) else { return }
        state.devices[i].snapshots[s] = snap
    }

    func restoreSnapshot(_ dev: String, _ snap: SettingsSnapshot) {
        guard let i = index(dev), let values = snap.values else { return }
        state.devices[i].settings = values
        ticks = ticks.filter { !$0.hasPrefix(dev + ":") }
        log("\(state.devices[i].name): restored settings from “\(snap.title)”")
    }

    func deleteSnapshot(_ dev: String, _ snap: SettingsSnapshot) {
        guard let i = index(dev) else { return }
        state.devices[i].snapshots.removeAll { $0.id == snap.id }
        for f in snap.images { try? FileManager.default.removeItem(at: Store.photoURL(dev, f)) }
    }

    // MARK: Care log

    func addCare(_ dev: String, _ entry: CareEntry) {
        guard let i = index(dev) else { return }
        state.devices[i].care.append(entry)
        state.devices[i].care.sort { $0.date > $1.date }
        log("\(state.devices[i].name): \(entry.kind.label.lowercased()) logged")
    }

    func deleteCare(_ dev: String, _ entry: CareEntry) {
        guard let i = index(dev) else { return }
        state.devices[i].care.removeAll { $0.id == entry.id }
    }

    // MARK: Connections

    func saveConn(_ c: Conn, isNew: Bool) {
        if isNew {
            state.conns.append(c)
        } else if let i = state.conns.firstIndex(where: { $0.id == c.id }) {
            state.conns[i] = c
        }
        log("\(isNew ? "Patched" : "Changed"): \(state.endText(c.from)) → \(state.endText(c.to))\(c.isPlanned ? " (planned)" : "")")
    }

    func deleteConn(_ c: Conn) {
        let text = "\(state.endText(c.from)) → \(state.endText(c.to))"
        state.conns.removeAll { $0.id == c.id }
        log("Unpatched: \(text)")
    }

    func toggleVerified(_ c: Conn) {
        guard let i = state.conns.firstIndex(where: { $0.id == c.id }) else { return }
        state.conns[i].verified.toggle()
        if state.conns[i].verified {
            log("Checked: \(state.endText(c.from)) → \(state.endText(c.to))")
        }
    }

    // MARK: Setups

    func saveSetup(named name: String) {
        var settings: [String: PanelValues] = [:]
        var canvas: [String: NodePos] = [:]
        for d in state.devices {
            if !d.settings.isEmpty { settings[d.id] = d.settings }
            if let p = d.canvas { canvas[d.id] = p }
        }
        let snap = Snapshot(conns: state.conns, settings: settings, canvas: canvas)
        guard let data = try? JSONCoding.encoder.encode(snap), let json = String(data: data, encoding: .utf8) else { return }
        let df = DateFormatter()
        df.dateStyle = .medium
        df.timeStyle = .short
        let finalName = name.trimmingCharacters(in: .whitespaces).isEmpty ? "Setup \(df.string(from: Date()))" : name
        setups.insert(Setup(id: newID(), name: finalName, ts: nowMS(), json: json), at: 0)
        log("Saved setup “\(finalName)”")
        // The SSL 18 can reset to defaults, so every setup also leaves a dated snapshot on its record.
        if state.device("ssl18") != nil {
            takeSnapshot("ssl18", title: "Setup: \(finalName)")
        }
    }

    func snapshot(of setup: Setup) -> Snapshot? {
        guard let data = setup.json.data(using: .utf8) else { return nil }
        return try? JSONCoding.decoder.decode(Snapshot.self, from: data)
    }

    func loadSetup(_ setup: Setup) {
        guard let snap = snapshot(of: setup) else { return }
        var s = state
        let known = Set(s.devices.map { $0.id })
        s.conns = snap.conns.filter { known.contains($0.from.dev) && known.contains($0.to.dev) }
        for i in s.devices.indices {
            let id = s.devices[i].id
            if let v = snap.settings[id] { s.devices[i].settings = v }
            if let p = snap.canvas[id], s.devices[i].onCanvasAllowed { s.devices[i].canvas = p }
        }
        state = s
        ticks.removeAll()
        log("Loaded setup “\(setup.name)”")
    }

    func deleteSetup(_ setup: Setup) {
        setups.removeAll { $0.id == setup.id }
    }

    // MARK: Backup & restore

    func copyToClipboard(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }

    func dateStamp() -> String {
        let df = DateFormatter()
        df.dateFormat = "yyyy-MM-dd"
        return df.string(from: Date())
    }

    /// JSON only (no images).
    func exportJSON() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.json]
        panel.nameFieldStringValue = "PatchBook-\(dateStamp()).json"
        guard panel.runModal() == .OK, let url = panel.url,
              let data = try? JSONCoding.encoder.encode(state) else { return }
        try? data.write(to: url, options: .atomic)
    }

    /// A folder with state.json, setups.json and all photos.
    func exportFullBackup() -> Bool {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.prompt = "Save backup here"
        guard panel.runModal() == .OK, let parent = panel.url else { return true }
        let target = parent.appendingPathComponent("PatchBook backup \(dateStamp())", isDirectory: true)
        let fm = FileManager.default
        do {
            if fm.fileExists(atPath: target.path) { try fm.removeItem(at: target) }
            try fm.createDirectory(at: target, withIntermediateDirectories: true)
            saveNow()
            try fm.copyItem(at: Store.stateURL, to: target.appendingPathComponent("state.json"))
            if fm.fileExists(atPath: Store.setupsURL.path) {
                try fm.copyItem(at: Store.setupsURL, to: target.appendingPathComponent("setups.json"))
            }
            if fm.fileExists(atPath: Store.photosFolder.path) {
                try fm.copyItem(at: Store.photosFolder, to: target.appendingPathComponent("Photos"))
            }
            NSWorkspace.shared.activateFileViewerSelecting([target])
            return true
        } catch {
            return false
        }
    }

    /// Restore from a JSON file or from a full-backup folder. Returns a message for the user.
    func restore() -> String? {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = true
        panel.allowedContentTypes = [.json, .folder]
        panel.prompt = "Restore"
        guard panel.runModal() == .OK, let url = panel.url else { return nil }
        let fm = FileManager.default
        var isDir: ObjCBool = false
        fm.fileExists(atPath: url.path, isDirectory: &isDir)
        let stateFile = isDir.boolValue ? url.appendingPathComponent("state.json") : url
        guard let data = try? Data(contentsOf: stateFile),
              let s = try? JSONCoding.decoder.decode(StudioState.self, from: data), !s.devices.isEmpty else {
            return "That isn't a Patch Book backup."
        }
        if isDir.boolValue {
            let photos = url.appendingPathComponent("Photos")
            if fm.fileExists(atPath: photos.path) {
                try? fm.removeItem(at: Store.photosFolder)
                try? fm.copyItem(at: photos, to: Store.photosFolder)
            }
            if let sd = try? Data(contentsOf: url.appendingPathComponent("setups.json")),
               let l = try? JSONCoding.decoder.decode([Setup].self, from: sd) {
                setups = l
            }
        }
        state = s
        ticks.removeAll()
        log("Restored from backup")
        return isDir.boolValue ? "Backup restored, including photos." : "Backup restored. Photos aren't in a JSON file, so existing photos were kept."
    }

    func revealDataFolder() {
        NSWorkspace.shared.activateFileViewerSelecting([Store.stateURL])
    }
}
