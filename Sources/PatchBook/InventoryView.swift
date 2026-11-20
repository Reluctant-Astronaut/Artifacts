import SwiftUI

/// One line in the inventory table (plain values so every column can sort).
struct InventoryRow: Identifiable {
    let id: String
    let name: String
    let makerModel: String
    let category: String
    let location: String
    let status: String
    let statusValue: GearStatus
    let year: String
    let value: Double          // replacement value; -1 when unknown (sorts last when descending)
    let photo: URL?
    let patchable: Bool
}

func nok(_ v: Double?) -> String {
    guard let v = v else { return "—" }
    let f = NumberFormatter()
    f.numberStyle = .decimal
    f.locale = Locale(identifier: "nb_NO")
    f.maximumFractionDigits = 0
    return "kr " + (f.string(from: NSNumber(value: v)) ?? String(Int(v)))
}

func statusColor(_ s: GearStatus) -> Color {
    switch s {
    case .inUse: return Theme.ok
    case .spare: return Theme.mute
    case .underRepair: return Theme.red
    case .modInProgress: return Theme.amber
    case .lentOut: return Theme.blue
    case .sold: return Theme.dim
    }
}

struct InventoryView: View {
    @EnvironmentObject var store: Store
    let onOpenDevice: (String) -> Void

    @State var query: String = ""
    @State var category: String = ""
    @State var location: String = ""
    @State var status: String = ""
    @State var showSold: Bool = false
    @State var sortOrder: [KeyPathComparator<InventoryRow>] = [KeyPathComparator(\InventoryRow.name)]
    @State var selection: Set<String> = []
    @State var showAdd: Bool = false
    @State var showPDF: Bool = false
    @State var message: String?

    var rows: [InventoryRow] {
        let q = query.lowercased().trimmingCharacters(in: .whitespaces)
        return store.state.devices.filter { d in
            if d.isSold && !showSold && status != GearStatus.sold.rawValue { return false }
            if !category.isEmpty && d.category.rawValue != category { return false }
            if location == "none" && d.location != nil { return false }
            if !location.isEmpty && location != "none" && d.location?.rawValue != location { return false }
            if !status.isEmpty && d.status.rawValue != status { return false }
            if !q.isEmpty {
                let hay = [d.name, d.maker, d.model, d.serial, d.notes, d.whereBought].joined(separator: " ").lowercased()
                if !hay.contains(q) { return false }
            }
            return true
        }.map { d in
            InventoryRow(id: d.id, name: d.name, makerModel: d.makerModel, category: d.category.label,
                         location: d.location?.label ?? "", status: d.status.label, statusValue: d.status,
                         year: d.yearMade, value: d.replacementValue ?? -1, photo: d.frontPhotoURL,
                         patchable: d.patchable)
        }
        .sorted(using: sortOrder)
    }

    var body: some View {
        let list = rows
        let total = list.reduce(0.0) { $0 + max(0, $1.value) }
        let missing = list.filter { $0.value < 0 }.count
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                TextField("Search name, maker, serial, notes…", text: $query)
                    .textFieldStyle(.roundedBorder)
                    .frame(minWidth: 200)
                Picker("", selection: $category) {
                    Text("All categories").tag("")
                    Divider()
                    ForEach(GearCategory.allCases) { Text($0.label).tag($0.rawValue) }
                }
                .labelsHidden()
                .frame(width: 150)
                Picker("", selection: $location) {
                    Text("All locations").tag("")
                    Text("No location set").tag("none")
                    Divider()
                    ForEach(Location.allCases) { Text($0.label).tag($0.rawValue) }
                }
                .labelsHidden()
                .frame(width: 150)
                Picker("", selection: $status) {
                    Text("Any status").tag("")
                    Divider()
                    ForEach(GearStatus.allCases) { Text($0.label).tag($0.rawValue) }
                }
                .labelsHidden()
                .frame(width: 140)
                Toggle("Show sold", isOn: $showSold)
                    .toggleStyle(.checkbox)
                Spacer(minLength: 4)
                Menu {
                    Button("Spreadsheet (CSV)…") {
                        message = Exports.saveCSV(store.state)
                    }
                    Button("Insurance list (PDF)…") { showPDF = true }
                } label: {
                    Label("Export", systemImage: "square.and.arrow.up")
                }
                .fixedSize()
                Button {
                    showAdd = true
                } label: {
                    Label("Device", systemImage: "plus")
                }
            }
            .padding(.horizontal, 16)
            .padding(.vertical, 10)

            Table(list, selection: $selection, sortOrder: $sortOrder) {
                TableColumn("Name", value: \InventoryRow.name) { r in
                    HStack(spacing: 8) {
                        Thumb(url: r.photo, width: 30, height: 30, corner: 5)
                        VStack(alignment: .leading, spacing: 1) {
                            Text(r.name).font(.system(size: 13, weight: .medium))
                            if !r.patchable {
                                Text("not patchable").font(.system(size: 10)).foregroundColor(Theme.dim)
                            }
                        }
                    }
                    .opacity(r.statusValue == .sold ? 0.5 : 1)
                }
                .width(min: 180, ideal: 240)
                TableColumn("Maker / model", value: \InventoryRow.makerModel) { r in
                    Text(r.makerModel.isEmpty ? "—" : r.makerModel)
                        .foregroundColor(r.makerModel.isEmpty ? Theme.dim : Theme.text)
                }
                .width(min: 140, ideal: 220)
                TableColumn("Category", value: \InventoryRow.category)
                    .width(min: 80, ideal: 100)
                TableColumn("Location", value: \InventoryRow.location) { r in
                    Text(r.location.isEmpty ? "—" : r.location)
                        .foregroundColor(r.location.isEmpty ? Theme.dim : Theme.text)
                }
                .width(min: 90, ideal: 120)
                TableColumn("Status", value: \InventoryRow.status) { r in
                    HStack(spacing: 6) {
                        Circle().fill(statusColor(r.statusValue)).frame(width: 7, height: 7)
                        Text(r.status)
                    }
                }
                .width(min: 90, ideal: 130)
                TableColumn("Year", value: \InventoryRow.year) { r in
                    Text(r.year.isEmpty ? "—" : r.year).foregroundColor(r.year.isEmpty ? Theme.dim : Theme.text)
                }
                .width(min: 50, ideal: 70)
                TableColumn("Value", value: \InventoryRow.value) { r in
                    Text(r.value < 0 ? "—" : nok(r.value))
                        .font(.system(size: 12, design: .monospaced))
                        .foregroundColor(r.value < 0 ? Theme.dim : Theme.text)
                        .frame(maxWidth: .infinity, alignment: .trailing)
                }
                .width(min: 90, ideal: 110)
            }
            .contextMenu(forSelectionType: String.self) { ids in
                if let id = ids.first {
                    Button("Open device page") { onOpenDevice(id) }
                }
            } primaryAction: { ids in
                if let id = ids.first { onOpenDevice(id) }
            }

            HStack(spacing: 14) {
                Text("\(list.count) item\(list.count == 1 ? "" : "s")")
                Text("Replacement value: \(nok(total))")
                if missing > 0 {
                    Text("\(missing) without a value").foregroundColor(Theme.amber)
                }
                Spacer()
                if let m = message {
                    Text(m).foregroundColor(Theme.ok)
                }
                Text("Double-click a row to open it")
                    .foregroundColor(Theme.dim)
            }
            .font(.system(size: 12))
            .foregroundColor(Theme.mute)
            .padding(.horizontal, 16)
            .padding(.vertical, 8)
        }
        .sheet(isPresented: $showAdd) {
            AddDeviceSheet { id in onOpenDevice(id) }
                .environmentObject(store)
        }
        .sheet(isPresented: $showPDF) {
            InsurancePDFSheet { m in message = m }
                .environmentObject(store)
        }
    }
}

struct AddDeviceSheet: View {
    @EnvironmentObject var store: Store
    @Environment(\.dismiss) var dismiss
    let onCreated: (String) -> Void
    @State var name: String = ""
    @State var category: GearCategory = .other

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("New device").font(.title2.weight(.bold))
            Form {
                TextField("Name", text: $name)
                Picker("Category", selection: $category) {
                    ForEach(GearCategory.allCases) { Text($0.label).tag($0) }
                }
            }
            Text("You can fill in the rest — jacks, photos, purchase details — on its device page.")
                .font(.system(size: 12))
                .foregroundColor(Theme.mute)
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Add") {
                    let id = store.addDevice(name: name.trimmingCharacters(in: .whitespaces), category: category)
                    dismiss()
                    onCreated(id)
                }
                .keyboardShortcut(.defaultAction)
                .disabled(name.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
        .padding(22)
        .frame(width: 420)
    }
}

struct InsurancePDFSheet: View {
    @EnvironmentObject var store: Store
    @Environment(\.dismiss) var dismiss
    let onDone: (String) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Insurance list (PDF)").font(.title2.weight(.bold))
            Text("In Norwegian, with a cover page, a photo of each item and the total replacement value. Sold gear is left out.")
                .font(.system(size: 12))
                .foregroundColor(Theme.mute)
            Form {
                TextField("Name", text: $store.state.owner.name)
                TextField("Address", text: $store.state.owner.address, axis: .vertical)
                    .lineLimit(2...4)
            }
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Create PDF…") {
                    let m = Exports.saveInsurancePDF(store.state)
                    dismiss()
                    if let m = m { onDone(m) }
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding(22)
        .frame(width: 460)
    }
}
