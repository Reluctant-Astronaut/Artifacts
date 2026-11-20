import Foundation

// MARK: - Helpers

extension KeyedDecodingContainer {
    /// Decode a value, or fall back when it is missing or malformed (keeps old/partial files readable).
    func get<T: Decodable>(_ key: Key, _ fallback: T) -> T {
        if let value = try? decodeIfPresent(T.self, forKey: key) { return value }
        return fallback
    }
}

func newID() -> String {
    String(UUID().uuidString.prefix(8)).lowercased()
}

/// Stable id for a jack, made from its original name: "Out 1+2" -> "out-1+2".
func pid(_ name: String) -> String {
    name.lowercased()
        .replacingOccurrences(of: " ", with: "-")
        .replacingOccurrences(of: "/", with: "-")
        .replacingOccurrences(of: "(", with: "")
        .replacingOccurrences(of: ")", with: "")
}

/// Shared JSON settings so dates are readable (ISO 8601) everywhere.
enum JSONCoding {
    static var encoder: JSONEncoder {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        e.dateEncodingStrategy = .iso8601
        return e
    }
    static var decoder: JSONDecoder {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }
}

/// Settings of one device's panel: control key -> value ("1"/"0" for switches, the option for choices, text for notes).
typealias PanelValues = [String: String]

// MARK: - Fixed lists

enum GearCategory: String, Codable, CaseIterable, Identifiable {
    case interface, preamp, compressor, mic, instrument, synth, speaker, headphones, pedal, playback, diy, infrastructure, other
    var id: String { rawValue }
    var label: String {
        switch self {
        case .diy: return "DIY"
        default: return rawValue.capitalized
        }
    }
    var norwegian: String {
        switch self {
        case .interface: return "Lydkort"
        case .preamp: return "Forforsterker"
        case .compressor: return "Kompressor"
        case .mic: return "Mikrofon"
        case .instrument: return "Instrument"
        case .synth: return "Synth"
        case .speaker: return "Høyttaler"
        case .headphones: return "Hodetelefoner"
        case .pedal: return "Pedal"
        case .playback: return "Avspilling"
        case .diy: return "Hjemmelaget"
        case .infrastructure: return "Infrastruktur"
        case .other: return "Annet"
        }
    }
}

enum Location: String, Codable, CaseIterable, Identifiable {
    case rack, desk, controlRoom = "control room", trackingCorner = "tracking corner", guitarWall = "guitar wall", storage
    var id: String { rawValue }
    var label: String { rawValue.prefix(1).uppercased() + rawValue.dropFirst() }
    var norwegian: String {
        switch self {
        case .rack: return "Rack"
        case .desk: return "Pult"
        case .controlRoom: return "Kontrollrom"
        case .trackingCorner: return "Innspillingshjørne"
        case .guitarWall: return "Gitarvegg"
        case .storage: return "Lager"
        }
    }
}

enum GearStatus: String, Codable, CaseIterable, Identifiable {
    case inUse = "in use", spare, underRepair = "under repair", modInProgress = "mod in progress", lentOut = "lent out", sold
    var id: String { rawValue }
    var label: String { rawValue.prefix(1).uppercased() + rawValue.dropFirst() }
    var norwegian: String {
        switch self {
        case .inUse: return "I bruk"
        case .spare: return "Reserve"
        case .underRepair: return "Til reparasjon"
        case .modInProgress: return "Under modifisering"
        case .lentOut: return "Utlånt"
        case .sold: return "Solgt"
        }
    }
}

enum Connector: String, Codable, CaseIterable, Identifiable {
    case xlr = "XLR", trs = "TRS", ts = "TS", combo = "XLR/TRS combo", rca = "RCA", adat = "ADAT",
         spdif = "S/PDIF", usb = "USB", midi = "MIDI", speaker = "Speaker", other = "Other"
    var id: String { rawValue }
}

enum PortDirection: String, Codable { case input, output }

enum PhotoKind: String, Codable, CaseIterable, Identifiable {
    case front, back, serial = "serial plate", receipt, other
    var id: String { rawValue }
    var label: String { rawValue.prefix(1).uppercased() + rawValue.dropFirst() }
}

enum CareKind: String, Codable, CaseIterable, Identifiable {
    case repair, mod, service, restringing, tuning, other
    var id: String { rawValue }
    var label: String { rawValue.prefix(1).uppercased() + rawValue.dropFirst() }
}

/// The "cable" type for a cab → mic step: drawn dotted on the canvas, needs no jacks.
let micdLink = "Mic'd (acoustic)"
let cableTypes = ["XLR", "TRS", "TS", "RCA", "Speaker", "ADAT", "S/PDIF", "MIDI", "USB", "Other", micdLink]

// MARK: - Records

/// One jack. The id never changes, so cables and panels keep working when the name is edited.
/// A stereo pair ("Out 1+2") is a jack whose `members` are the ids of its two single jacks.
struct Jack: Codable, Identifiable, Hashable {
    var id: String
    var name: String
    var direction: PortDirection
    var connector: Connector
    var members: [String] = []

    var isPair: Bool { !members.isEmpty }

    init(id: String? = nil, name: String, direction: PortDirection, connector: Connector, members: [String] = []) {
        self.id = id ?? pid(name)
        self.name = name
        self.direction = direction
        self.connector = connector
        self.members = members
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = c.get(.name, "Jack")
        id = c.get(.id, pid(name))
        direction = c.get(.direction, PortDirection.input)
        connector = c.get(.connector, Connector.other)
        members = c.get(.members, [String]())
    }
}

struct PhotoRef: Codable, Identifiable, Hashable {
    var id: String = newID()
    var kind: PhotoKind = .front
    var file: String               // file name inside Photos/<device id>/
    var caption: String = ""
    var added: Date = Date()
}

struct SettingsSnapshot: Codable, Identifiable, Hashable {
    var id: String = newID()
    var date: Date = Date()
    var title: String = ""
    var note: String = ""
    var values: PanelValues?       // panel switch positions, for devices that have a panel
    var images: [String] = []      // file names inside Photos/<device id>/
}

struct CareEntry: Codable, Identifiable, Hashable {
    var id: String = newID()
    var date: Date = Date()
    var kind: CareKind = .service
    var text: String = ""
    var cost: Double?
    var by: String = ""
}

/// Where a device's box sits on the Signal Flow canvas (centre point).
struct NodePos: Codable, Hashable {
    var x: Double
    var y: Double
}

/// The one record for a piece of gear. The inventory, patch tabs and canvas all read from it.
struct Device: Codable, Identifiable, Hashable {
    var id: String = newID()
    var name: String = ""
    var maker: String = ""
    var model: String = ""
    var category: GearCategory = .other
    var serial: String = ""
    var yearMade: String = ""
    var purchaseDate: Date?
    var purchasePrice: Double?         // NOK
    var replacementValue: Double?      // NOK, for insurance
    var whereBought: String = ""
    var location: Location?
    var status: GearStatus = .inUse
    var soldDate: Date?
    var soldPrice: Double?
    var notes: String = ""
    var patchable: Bool = true
    var ports: [Jack] = []
    var settings: PanelValues = [:]    // panel switch positions (SSL 18 etc.)
    var canvas: NodePos?               // position on the Signal Flow canvas
    var photos: [PhotoRef] = []
    var snapshots: [SettingsSnapshot] = []
    var care: [CareEntry] = []

    init(id: String = newID(), name: String = "") {
        self.id = id
        self.name = name
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.get(.id, newID())
        name = c.get(.name, "Unnamed")
        maker = c.get(.maker, "")
        model = c.get(.model, "")
        category = c.get(.category, GearCategory.other)
        serial = c.get(.serial, "")
        yearMade = c.get(.yearMade, "")
        purchaseDate = c.get(.purchaseDate, Date?.none)
        purchasePrice = c.get(.purchasePrice, Double?.none)
        replacementValue = c.get(.replacementValue, Double?.none)
        whereBought = c.get(.whereBought, "")
        location = c.get(.location, Location?.none)
        status = c.get(.status, GearStatus.inUse)
        soldDate = c.get(.soldDate, Date?.none)
        soldPrice = c.get(.soldPrice, Double?.none)
        notes = c.get(.notes, "")
        patchable = c.get(.patchable, true)
        ports = c.get(.ports, [Jack]())
        settings = c.get(.settings, PanelValues())
        canvas = c.get(.canvas, NodePos?.none)
        photos = c.get(.photos, [PhotoRef]())
        snapshots = c.get(.snapshots, [SettingsSnapshot]())
        care = c.get(.care, [CareEntry]())
    }

    var inputs: [Jack] { ports.filter { $0.direction == .input } }
    var outputs: [Jack] { ports.filter { $0.direction == .output } }
    var isSold: Bool { status == .sold }
    var onCanvasAllowed: Bool { patchable && !ports.isEmpty && !isSold }
    var makerModel: String { [maker, model].filter { !$0.isEmpty }.joined(separator: " ") }
    var frontPhoto: PhotoRef? { photos.first { $0.kind == .front } ?? photos.first }

    func port(_ id: String) -> Jack? { ports.first { $0.id == id } }

    /// The jack ids a cable on `id` occupies: a pair counts for both its members.
    func expand(_ id: String) -> [String] {
        guard let p = port(id) else { return [id] }
        return [id] + p.members
    }
}

struct End: Codable, Hashable {
    var dev: String = ""
    var port: String = ""          // port id; "" = not chosen yet

    init(dev: String = "", port: String = "") {
        self.dev = dev
        self.port = port
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        dev = c.get(.dev, "")
        port = c.get(.port, "")
    }
}

struct Conn: Codable, Identifiable, Hashable {
    var id: String = newID()
    var from: End = End()
    var to: End = End()
    var cable: String = ""
    var status: String = "active"      // "active" or "planned"
    var verified: Bool = false
    var notes: String = ""

    init(from: End = End(), to: End = End(), cable: String = "", status: String = "active") {
        self.from = from
        self.to = to
        self.cable = cable
        self.status = status
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = c.get(.id, newID())
        from = c.get(.from, End())
        to = c.get(.to, End())
        cable = c.get(.cable, "")
        status = c.get(.status, "active")
        verified = c.get(.verified, false)
        notes = c.get(.notes, "")
    }

    var isPlanned: Bool { status == "planned" }
    var isMicd: Bool { cable == micdLink }
}

struct LogEntry: Codable, Hashable {
    var ts: Double
    var msg: String
}

/// Name and address for the cover page of the insurance list.
struct Owner: Codable, Hashable {
    var name: String = ""
    var address: String = ""
}

struct StudioState: Codable {
    var v: Int = 3
    var owner: Owner = Owner()
    var devices: [Device] = []
    var conns: [Conn] = []
    var log: [LogEntry] = []

    init() {}

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        v = 3
        owner = c.get(.owner, Owner())
        devices = c.get(.devices, [Device]())
        conns = c.get(.conns, [Conn]())
        log = c.get(.log, [LogEntry]())
    }
}

/// A saved setup: cables plus each device's panel settings and canvas position.
/// It refers to devices by id and never stores copies of them.
struct Snapshot: Codable {
    var conns: [Conn] = []
    var settings: [String: PanelValues] = [:]
    var canvas: [String: NodePos] = [:]

    init(conns: [Conn], settings: [String: PanelValues], canvas: [String: NodePos]) {
        self.conns = conns
        self.settings = settings
        self.canvas = canvas
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        conns = c.get(.conns, [Conn]())
        settings = c.get(.settings, [String: PanelValues]())
        canvas = c.get(.canvas, [String: NodePos]())
    }
}

struct Setup: Codable, Identifiable, Hashable {
    var id: String
    var name: String
    var ts: Double
    var json: String
}

struct CheckItem: Identifiable, Hashable {
    let id: String
    let text: String
}

struct DiffLine: Identifiable, Hashable {
    enum Kind { case add, remove, change }
    let id = UUID()
    let kind: Kind
    let text: String
}

enum Side { case from, to }

func nowMS() -> Double {
    Date().timeIntervalSince1970 * 1000
}

// MARK: - Queries

extension StudioState {
    func device(_ id: String) -> Device? { devices.first { $0.id == id } }

    func devName(_ id: String) -> String {
        if let d = device(id) { return d.name }
        return id.isEmpty ? "?" : id
    }

    func portName(_ e: End) -> String {
        if e.port.isEmpty { return "?" }
        return device(e.dev)?.port(e.port)?.name ?? e.port
    }

    func endText(_ e: End) -> String {
        "\(devName(e.dev)) · \(portName(e))"
    }

    var activeConns: [Conn] { conns.filter { !$0.isPlanned } }

    func value(_ dev: String, _ key: String, _ fallback: String) -> String {
        device(dev)?.settings[key] ?? fallback
    }

    func expand(_ e: End) -> [String] {
        device(e.dev)?.expand(e.port) ?? [e.port]
    }

    /// Cables arriving at any of the jack ids on `dev` (a cable on a pair counts for both members).
    func conns(into dev: String, ids: [String], includePlanned: Bool = false) -> [Conn] {
        let wanted = Set(ids)
        return (includePlanned ? conns : activeConns).filter { c in
            c.to.dev == dev && !c.to.port.isEmpty && !wanted.isDisjoint(with: expand(c.to))
        }
    }

    func conns(outOf dev: String, ids: [String], includePlanned: Bool = false) -> [Conn] {
        let wanted = Set(ids)
        return (includePlanned ? conns : activeConns).filter { c in
            c.from.dev == dev && !c.from.port.isEmpty && !wanted.isDisjoint(with: expand(c.from))
        }
    }

    static func inputKey(_ c: Conn) -> String {
        c.to.dev + "|" + c.to.port
    }

    /// Inputs fed by more than one active cable (checked per single jack, so a pair and its member clash too).
    func clashKeys() -> Set<String> {
        var counts: [String: Int] = [:]
        for c in activeConns where !c.to.port.isEmpty && !c.isMicd {
            for p in Set(expand(c.to)) { counts[c.to.dev + "|" + p, default: 0] += 1 }
        }
        let busy = Set(counts.filter { $0.value > 1 }.map { $0.key })
        return Set(activeConns.filter { c in expand(c.to).contains { busy.contains(c.to.dev + "|" + $0) } }.map(StudioState.inputKey))
    }

    static func hasUnknownPort(_ c: Conn) -> Bool {
        if c.isMicd { return false }
        return c.from.port.isEmpty || c.to.port.isEmpty
    }

    var activeDevices: [Device] { devices.filter { !$0.isSold } }
}
