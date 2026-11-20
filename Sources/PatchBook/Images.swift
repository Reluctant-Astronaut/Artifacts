import SwiftUI
import AppKit
import ImageIO
import ScreenCaptureKit

// MARK: - Thumbnails

/// Small, cached versions of photos, so lists and the PDF don't load full-size images.
enum Thumbs {
    static let cache = NSCache<NSString, NSImage>()

    static func image(_ url: URL, max: Int) -> NSImage? {
        let key = "\(url.path)#\(max)" as NSString
        if let cached = cache.object(forKey: key) { return cached }
        guard let src = CGImageSourceCreateWithURL(url as CFURL, nil) else { return nil }
        let opts: [CFString: Any] = [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceThumbnailMaxPixelSize: max,
            kCGImageSourceCreateThumbnailWithTransform: true
        ]
        guard let cg = CGImageSourceCreateThumbnailAtIndex(src, 0, opts as CFDictionary) else { return nil }
        let img = NSImage(cgImage: cg, size: NSSize(width: cg.width, height: cg.height))
        cache.setObject(img, forKey: key)
        return img
    }
}

struct Thumb: View {
    let url: URL?
    let width: CGFloat
    let height: CGFloat
    var corner: CGFloat = 6

    var body: some View {
        Group {
            if let u = url, let img = Thumbs.image(u, max: Int(max(width, height) * 2)) {
                Image(nsImage: img)
                    .resizable()
                    .aspectRatio(contentMode: .fill)
            } else {
                ZStack {
                    Theme.panel2
                    Image(systemName: "photo")
                        .font(.system(size: min(width, height) * 0.35))
                        .foregroundColor(Theme.dim)
                }
            }
        }
        .frame(width: width, height: height)
        .clipShape(RoundedRectangle(cornerRadius: corner))
    }
}

extension Device {
    var frontPhotoURL: URL? {
        frontPhoto.map { Store.photoURL(id, $0.file) }
    }
}

// MARK: - Capturing the SSL 360 window

enum WindowCapture {
    enum CaptureError: LocalizedError {
        case notFound, encode

        var errorDescription: String? {
            switch self {
            case .notFound:
                return "Couldn't find an open SSL 360 window. Open SSL 360 and try again."
            case .encode:
                return "Couldn't save the screenshot."
            }
        }
    }

    /// Takes a PNG of the SSL 360 window. The first time, macOS asks for Screen Recording permission
    /// (System Settings → Privacy & Security → Screen & System Audio Recording); Patch Book must be
    /// reopened after it is granted.
    static func captureSSL360() async throws -> Data {
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        let candidates = content.windows.filter { w in
            let app = (w.owningApplication?.applicationName ?? "").lowercased()
            let title = (w.title ?? "").lowercased()
            return app.contains("ssl 360") || app.contains("ssl360") || title.contains("ssl 360")
        }
        guard let win = candidates.max(by: { $0.frame.width * $0.frame.height < $1.frame.width * $1.frame.height }) else {
            throw CaptureError.notFound
        }
        let filter = SCContentFilter(desktopIndependentWindow: win)
        let config = SCStreamConfiguration()
        config.width = Int(win.frame.width * 2)
        config.height = Int(win.frame.height * 2)
        config.showsCursor = false
        let image = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: config)
        let rep = NSBitmapImageRep(cgImage: image)
        guard let png = rep.representation(using: .png, properties: [:]) else { throw CaptureError.encode }
        return png
    }
}
