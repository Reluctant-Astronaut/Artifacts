import SwiftUI
import AppKit

func formatDate(_ ms: Double) -> String {
    let df = DateFormatter()
    df.dateStyle = .medium
    df.timeStyle = .short
    return df.string(from: Date(timeIntervalSince1970: ms / 1000))
}

struct SetupsView: View {
    @EnvironmentObject var store: Store
    @State var newName: String = ""
    @State var compareID: String?
    @State var pendingLoad: Setup?
    @State var pendingDelete: Setup?
    @State var confirmRestore = false
    @State var message: String?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                SectionHeader("Save the current state as a setup")
                HStack {
                    TextField("e.g. Vocal tracking · K2 through ISA One", text: $newName)
                        .textFieldStyle(.roundedBorder)
                        .onSubmit(save)
                    Button("Save setup", action: save)
                        .keyboardShortcut("s", modifiers: .command)
                }
                Text("A setup stores all connections and SSL 18 settings, so you can jump back to a known patch — or see exactly what changed.")
                    .font(.system(size: 12))
                    .foregroundColor(Theme.mute)

                SectionHeader("Saved setups · \(store.setups.count)")
                if store.setups.isEmpty {
                    Text("No setups saved yet.")
                        .foregroundColor(Theme.dim)
                        .frame(maxWidth: .infinity)
                        .padding(18)
                        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Theme.line, style: StrokeStyle(lineWidth: 1, dash: [5, 4])))
                }
                ForEach(store.setups) { setup in
                    VStack(alignment: .leading, spacing: 8) {
                        HStack(spacing: 10) {
                            Text(setup.name).font(.system(size: 14, weight: .semibold))
                            Spacer()
                            Text(formatDate(setup.ts))
                                .font(.system(size: 11, design: .monospaced))
                                .foregroundColor(Theme.mute)
                            Button(compareID == setup.id ? "Hide changes" : "Compare") {
                                compareID = compareID == setup.id ? nil : setup.id
                            }
                            Button("Load") { pendingLoad = setup }
                            Button { pendingDelete = setup } label: { Image(systemName: "xmark") }
                                .buttonStyle(.borderless)
                        }
                        if compareID == setup.id {
                            diffView(setup)
                        }
                    }
                    .card(padding: 11)
                }

                SectionHeader("Print & backup")
                HStack {
                    Button("Copy patch sheet") {
                        store.copyToClipboard(store.state.patchSheet())
                        flash("Patch sheet copied — paste it into Notes or Pages to print.")
                    }
                    Button("Full backup (with photos)…") {
                        if !store.exportFullBackup() { flash("The backup could not be written there.") }
                    }
                    Button("JSON backup…") { store.exportJSON() }
                    Button("Restore…") { confirmRestore = true }
                    Button("Show data folder") { store.revealDataFolder() }
                }
                if let m = message {
                    Text(m).font(.system(size: 12)).foregroundColor(Theme.ok)
                }
                Text("Everything is saved automatically in ~/Library/Application Support/PatchBook, and Time Machine backs it up. A full backup is a folder with the data and all photos; the JSON backup has the data only. Saving a setup also adds a dated snapshot to the SSL 18's record.")
                    .font(.system(size: 12))
                    .foregroundColor(Theme.mute)
            }
            .padding(20)
        }
        .confirmationDialog("Load this setup?",
                            isPresented: Binding(get: { pendingLoad != nil }, set: { if !$0 { pendingLoad = nil } }),
                            presenting: pendingLoad) { s in
            Button("Load “\(s.name)”") {
                store.loadSetup(s)
                compareID = nil
                flash("Loaded — check the reset list on the SSL 18 page.")
            }
        } message: { _ in
            Text("Your current cables, panel settings and canvas layout will be replaced. Inventory details are not touched. Save a setup first if you want to keep the current patch.")
        }
        .confirmationDialog("Delete this setup?",
                            isPresented: Binding(get: { pendingDelete != nil }, set: { if !$0 { pendingDelete = nil } }),
                            presenting: pendingDelete) { s in
            Button("Delete “\(s.name)”", role: .destructive) { store.deleteSetup(s) }
        }
        .confirmationDialog("Restore from a backup?", isPresented: $confirmRestore) {
            Button("Choose backup…", role: .destructive) {
                if let m = store.restore() { flash(m) }
            }
        } message: {
            Text("Pick a full-backup folder or a JSON file. Everything in Patch Book is replaced with the backup.")
        }
    }

    func save() {
        store.saveSetup(named: newName)
        newName = ""
        flash("Setup saved.")
    }

    func flash(_ text: String) {
        message = text
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) {
            if message == text { message = nil }
        }
    }

    @ViewBuilder
    func diffView(_ setup: Setup) -> some View {
        if let snap = store.snapshot(of: setup) {
            let lines = store.state.diff(to: snap)
            VStack(alignment: .leading, spacing: 3) {
                Text("Loading this setup would change:")
                    .font(.system(size: 12, weight: .semibold))
                if lines.isEmpty {
                    Text("Nothing — identical to the current state.").foregroundColor(Theme.mute)
                }
                ForEach(lines) { l in
                    Text(l.text)
                        .foregroundColor(l.kind == .add ? Theme.ok : l.kind == .remove ? Color(hex: 0xFF8A87) : Theme.amber)
                }
            }
            .font(.system(size: 12, design: .monospaced))
            .textSelection(.enabled)
            .padding(10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: 8).fill(Theme.well))
        } else {
            Text("This setup could not be read.").foregroundColor(Theme.red)
        }
    }
}

struct HistoryView: View {
    @EnvironmentObject var store: Store
    @State var confirmClear = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                SectionHeader("Change history") {
                    if !store.state.log.isEmpty {
                        Button("Clear") { confirmClear = true }.controlSize(.small)
                    }
                }
                .padding(.bottom, 8)
                if store.state.log.isEmpty {
                    Text("Changes you make show up here, with dates.")
                        .foregroundColor(Theme.dim)
                        .padding(.vertical, 18)
                }
                ForEach(Array(store.state.log.enumerated()), id: \.offset) { _, entry in
                    HStack(alignment: .firstTextBaseline, spacing: 14) {
                        Text(formatDate(entry.ts))
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundColor(Theme.dim)
                            .frame(width: 150, alignment: .leading)
                        Text(entry.msg)
                            .font(.system(size: 13))
                            .textSelection(.enabled)
                        Spacer(minLength: 0)
                    }
                    .padding(.vertical, 7)
                    Divider().overlay(Theme.line)
                }
            }
            .padding(20)
        }
        .confirmationDialog("Clear the change history?", isPresented: $confirmClear) {
            Button("Clear", role: .destructive) { store.state.log.removeAll() }
        }
    }
}
