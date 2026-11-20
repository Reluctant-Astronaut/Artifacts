// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "PatchBook",
    platforms: [.macOS(.v14)],
    targets: [
        .executableTarget(
            name: "PatchBook",
            path: "Sources/PatchBook"
        )
    ]
)
