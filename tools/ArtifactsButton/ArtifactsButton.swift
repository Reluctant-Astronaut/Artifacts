// Artifacts Button: one big button. Red = press me. Amber = working (fetching the GitHub build, installing).
// Green = Artifacts is installed and ready; open Cubase. The Dock icon changes colour with it.
// The work itself is done by press.sh (in Resources); it reports back with lines that start with @status, @done or @error.
//
// Built by the GitHub workflow:  swiftc -swift-version 5 -O ArtifactsButton.swift -framework AppKit
import AppKit

enum Lamp { case idle, working, done, failed }

func drawButton(in rect: NSRect, lamp: Lamp, pulse: CGFloat, pressed: Bool) {
    let s = min(rect.width, rect.height)
    let c = NSPoint(x: rect.midX, y: rect.midY)
    func circle(_ r: CGFloat, dy: CGFloat = 0) -> NSBezierPath {
        NSBezierPath(ovalIn: NSRect(x: c.x - r, y: c.y - r + dy, width: 2 * r, height: 2 * r))
    }
    // soft shadow, steel bezel, dark well
    NSColor(white: 0, alpha: 0.28).setFill(); circle(s * 0.47, dy: -s * 0.02).fill()
    NSGradient(starting: NSColor(white: 0.92, alpha: 1), ending: NSColor(white: 0.38, alpha: 1))?.draw(in: circle(s * 0.46), angle: -90)
    NSGradient(starting: NSColor(white: 0.30, alpha: 1), ending: NSColor(white: 0.80, alpha: 1))?.draw(in: circle(s * 0.43), angle: -90)
    NSColor(white: 0.07, alpha: 1).setFill(); circle(s * 0.405).fill()

    let base: NSColor
    switch lamp {
    case .idle:    base = NSColor(srgbRed: 0.86, green: 0.07, blue: 0.05, alpha: 1)
    case .working: base = NSColor(srgbRed: 0.97, green: 0.52 + 0.20 * pulse, blue: 0.04, alpha: 1)
    case .done:    base = NSColor(srgbRed: 0.10, green: 0.76, blue: 0.24, alpha: 1)
    case .failed:  base = NSColor(srgbRed: 0.42, green: 0.05, blue: 0.05, alpha: 1)
    }
    let r = s * 0.37 * (pressed ? 0.97 : 1.0)
    let dome = circle(r, dy: pressed ? -s * 0.006 : 0)
    let light = base.blended(withFraction: 0.50, of: .white) ?? base
    let dark = base.blended(withFraction: 0.55, of: .black) ?? base
    NSGradient(colors: [light, base, dark])?.draw(in: dome, relativeCenterPosition: NSPoint(x: -0.25, y: 0.35))
    if lamp == .working || lamp == .done {   // a glow from inside, like a lit lamp
        let glow = base.withAlphaComponent(lamp == .working ? 0.25 + 0.25 * pulse : 0.35)
        NSGradient(starting: glow, ending: glow.withAlphaComponent(0))?.draw(in: dome, relativeCenterPosition: NSPoint(x: 0, y: 0))
    }
    // the highlight on the dome
    let hl = NSBezierPath(ovalIn: NSRect(x: c.x - r * 0.58, y: c.y + r * 0.22, width: r * 0.92, height: r * 0.52))
    NSColor(white: 1, alpha: pressed ? 0.16 : 0.32).setFill(); hl.fill()
    if lamp == .done {   // a tick
        let t = NSBezierPath()
        t.move(to: NSPoint(x: c.x - r * 0.38, y: c.y - r * 0.02))
        t.line(to: NSPoint(x: c.x - r * 0.10, y: c.y - r * 0.32))
        t.line(to: NSPoint(x: c.x + r * 0.42, y: c.y + r * 0.30))
        t.lineWidth = r * 0.13; t.lineCapStyle = .round; t.lineJoinStyle = .round
        NSColor(white: 1, alpha: 0.9).setStroke(); t.stroke()
    }
}

final class ButtonView: NSView {
    var lamp: Lamp = .idle { didSet { needsDisplay = true } }
    var pulse: CGFloat = 0 { didSet { needsDisplay = true } }
    var pressed = false { didSet { needsDisplay = true } }
    var onClick: (() -> Void)?

    override func draw(_ dirtyRect: NSRect) { drawButton(in: bounds, lamp: lamp, pulse: pulse, pressed: pressed) }
    private func inside(_ e: NSEvent) -> Bool {
        let p = convert(e.locationInWindow, from: nil)
        let r = min(bounds.width, bounds.height) * 0.46
        return hypot(p.x - bounds.midX, p.y - bounds.midY) <= r
    }
    override func mouseDown(with e: NSEvent) { pressed = inside(e) }
    override func mouseDragged(with e: NSEvent) { if pressed != inside(e) { pressed = inside(e) } }
    override func mouseUp(with e: NSEvent) { if pressed { pressed = false; onClick?() } }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    let button = ButtonView(frame: NSRect(x: 30, y: 96, width: 260, height: 260))
    let label = NSTextField(wrappingLabelWithString: "Press to install Artifacts")
    var timer: Timer?
    var process: Process?
    var buffer = ""

    func applicationDidFinishLaunching(_ notification: Notification) {
        let menu = NSMenu(), item = NSMenuItem()
        menu.addItem(item)
        let sub = NSMenu()
        sub.addItem(withTitle: "Quit Artifacts Button", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        item.submenu = sub
        NSApp.mainMenu = menu

        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 320, height: 380),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "Artifacts"
        window.isReleasedWhenClosed = false
        label.frame = NSRect(x: 20, y: 18, width: 280, height: 70)
        label.alignment = .center
        label.font = NSFont.systemFont(ofSize: 13)
        label.textColor = .secondaryLabelColor
        window.contentView?.addSubview(button)
        window.contentView?.addSubview(label)
        button.onClick = { [weak self] in self?.clicked() }
        window.center()
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        setLamp(.idle)
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { process == nil }

    func setLamp(_ l: Lamp) {
        button.lamp = l
        let img = NSImage(size: NSSize(width: 512, height: 512), flipped: false) { rect in
            drawButton(in: rect, lamp: l, pulse: 0.5, pressed: false); return true
        }
        NSApp.applicationIconImage = img   // the Dock icon follows the button
    }

    func clicked() {
        switch button.lamp {
        case .working: return
        case .done: openCubase()   // green: one more press opens Cubase
        default: run()
        }
    }

    func openCubase() {
        let fm = FileManager.default
        var apps: [String] = []
        for dir in ["/Applications", "/Applications/Steinberg"] {
            for name in (try? fm.contentsOfDirectory(atPath: dir)) ?? [] where name.hasPrefix("Cubase") && name.hasSuffix(".app") {
                apps.append(dir + "/" + name)
            }
        }
        if let app = apps.sorted().last { NSWorkspace.shared.open(URL(fileURLWithPath: app)) }
        else { label.stringValue = "Cubase was not found in Applications. Open it the usual way." }
    }

    func run() {
        guard let script = Bundle.main.path(forResource: "press", ofType: "sh") else { label.stringValue = "press.sh is missing from the app."; setLamp(.failed); return }
        setLamp(.working)
        label.stringValue = "Starting..."
        timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
            guard let self = self else { return }
            self.button.pulse = CGFloat((sin(Date().timeIntervalSinceReferenceDate * 3.0) + 1.0) / 2.0)
        }
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/bin/bash")
        p.arguments = [script]
        var env = ProcessInfo.processInfo.environment
        env["ARTIFACTS_APP"] = "1"
        if let repo = Bundle.main.object(forInfoDictionaryKey: "ArtifactsRepo") as? String, !repo.isEmpty { env["ARTIFACTS_REPO"] = repo }
        env["ARTI_ICON"] = Bundle.main.path(forResource: "AppIcon", ofType: "icns") ?? ""
        p.environment = env
        let pipe = Pipe()
        p.standardOutput = pipe
        p.standardError = FileHandle.nullDevice
        buffer = ""
        pipe.fileHandleForReading.readabilityHandler = { [weak self] h in
            let data = h.availableData
            if data.isEmpty { return }
            let text = String(decoding: data, as: UTF8.self)
            DispatchQueue.main.async { self?.consume(text) }
        }
        p.terminationHandler = { [weak self] proc in
            let code = proc.terminationStatus
            DispatchQueue.main.async {
                pipe.fileHandleForReading.readabilityHandler = nil
                let rest = pipe.fileHandleForReading.readDataToEndOfFile()
                self?.consume(String(decoding: rest, as: UTF8.self) + "\n")
                self?.finished(code)
            }
        }
        do { try p.run(); process = p }
        catch { finished(127) }
    }

    func consume(_ text: String) {
        buffer += text
        while let nl = buffer.firstIndex(of: "\n") {
            let line = String(buffer[buffer.startIndex..<nl])
            buffer = String(buffer[buffer.index(after: nl)...])
            if line.hasPrefix("@status ") { label.stringValue = String(line.dropFirst(8)) }
            else if line.hasPrefix("@done ") { stop(); setLamp(.done); label.stringValue = String(line.dropFirst(6)) }
            else if line.hasPrefix("@error ") { stop(); setLamp(.failed); label.stringValue = String(line.dropFirst(7)) }
            else if line.hasPrefix("@cancel") { stop(); setLamp(.idle); label.stringValue = "Cancelled. Press to try again." }
        }
    }

    func stop() { timer?.invalidate(); timer = nil }

    func finished(_ code: Int32) {
        process = nil
        stop()
        if button.lamp == .working {   // the script ended without saying how it went
            setLamp(code == 0 ? .done : .failed)
            label.stringValue = code == 0 ? "Ready. Open Cubase." : "Something went wrong. Details: ~/Library/Logs/Artifacts Button.log"
        }
    }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()
