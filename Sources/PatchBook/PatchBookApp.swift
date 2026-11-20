import SwiftUI
import AppKit

@main
struct PatchBookApp: App {
    @StateObject var store = Store()

    init() {
        // Launched as a plain executable (e.g. `swift run`) it would otherwise have no Dock icon or menu.
        NSApplication.shared.setActivationPolicy(.regular)
    }

    var body: some Scene {
        WindowGroup("Patch Book") {
            ContentView()
                .environmentObject(store)
                .frame(minWidth: 940, minHeight: 620)
                .preferredColorScheme(.dark)
                .onReceive(NotificationCenter.default.publisher(for: NSApplication.willTerminateNotification)) { _ in
                    store.saveNow()
                }
        }
        .defaultSize(width: 1240, height: 860)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}

enum Pane: Hashable {
    case overview, flow, connections, inventory, setups, history
    case device(String)     // a device's patch tab (SSL 18, ISA One, …)
    case item(String)       // a device's inventory page

    var title: String {
        switch self {
        case .overview: return "Overview"
        case .flow: return "Signal Flow"
        case .connections: return "Connections"
        case .inventory: return "Inventory"
        case .setups: return "Setups"
        case .history: return "History"
        case .device(let id), .item(let id): return id
        }
    }

    var icon: String {
        switch self {
        case .overview: return "square.grid.2x2"
        case .flow: return "point.3.connected.trianglepath.dotted"
        case .connections: return "cable.connector"
        case .inventory: return "shippingbox"
        case .setups: return "square.stack.3d.up"
        case .history: return "clock.arrow.circlepath"
        case .device: return "slider.vertical.3"
        case .item: return "info.circle"
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var store: Store
    @State var pane: Pane? = .inventory
    @State var backPane: Pane = .inventory
    @State var deviceFilter: String = ""

    var panelsPresent: [DevicePanel] {
        Panels.all.filter { p in store.state.device(p.deviceID).map { !$0.isSold } ?? false }
    }

    func showConnections(_ id: String) {
        deviceFilter = id
        pane = .connections
    }

    func openDevice(_ id: String) {
        if let current = pane, case .item = current {} else { backPane = pane ?? .inventory }
        pane = .item(id)
    }

    func openPanel(_ id: String) {
        pane = .device(id)
    }

    var body: some View {
        NavigationSplitView {
            List(selection: $pane) {
                Section("Studio") {
                    ForEach([Pane.overview, .flow, .connections, .inventory], id: \.self) { p in
                        Label(p.title, systemImage: p.icon).tag(p)
                    }
                }
                Section("Patch tabs") {
                    ForEach(panelsPresent) { panel in
                        Label(store.state.devName(panel.deviceID), systemImage: "slider.vertical.3")
                            .tag(Pane.device(panel.deviceID))
                    }
                }
                Section("Library") {
                    ForEach([Pane.setups, .history], id: \.self) { p in
                        Label(p.title, systemImage: p.icon).tag(p)
                    }
                }
            }
            .navigationSplitViewColumnWidth(min: 180, ideal: 200, max: 260)
            .safeAreaInset(edge: .bottom) {
                HStack(spacing: 8) {
                    Circle().fill(Theme.greenDark).overlay(Circle().fill(Color(hex: 0xEFE9DA)).frame(width: 7, height: 7)).frame(width: 18, height: 18)
                    Text("One Bright Pearl Studio")
                        .font(.system(size: 11, weight: .medium))
                        .foregroundColor(Theme.mute)
                    Spacer()
                }
                .padding(12)
            }
        } detail: {
            Group {
                switch pane ?? .inventory {
                case .overview:
                    OverviewView(onOpenPanel: openPanel, onShowConnections: showConnections, onOpenDevice: openDevice)
                case .flow:
                    SignalFlowView(onOpenPanel: openPanel, onShowConnections: showConnections, onOpenDevice: openDevice)
                case .connections:
                    ConnectionsView(deviceFilter: $deviceFilter)
                case .inventory:
                    InventoryView(onOpenDevice: openDevice)
                case .setups:
                    SetupsView()
                case .history:
                    HistoryView()
                case .device(let id):
                    if let panel = Panels.panel(for: id) {
                        DevicePanelView(panel: panel, onShowConnections: showConnections, onOpenDevice: openDevice)
                            .id(id)
                    } else {
                        Text("No patch tab for this device.").foregroundColor(Theme.mute)
                    }
                case .item(let id):
                    DevicePageView(deviceID: id,
                                   onBack: { pane = backPane },
                                   onOpenPanel: openPanel,
                                   onShowOnCanvas: { pane = .flow })
                        .id(id)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(Theme.bg)
            .navigationTitle(navTitle)
        }
    }

    var navTitle: String {
        switch pane ?? .inventory {
        case .device(let id): return store.state.devName(id)
        case .item(let id): return store.state.devName(id)
        default: return (pane ?? .inventory).title
        }
    }
}
