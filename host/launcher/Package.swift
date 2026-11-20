// swift-tools-version:6.0
// steamac-vm: AppKit/Metal launcher for libkrun v1.19.6 (C API).
// libkrun headers/dylib location is supplied by build.sh (-Xcc -I / -Xlinker -L / rpath).
import PackageDescription
import Foundation

// Local builds may reuse the same checksum-verified SDK; clean checkouts use the pinned official archive.
let localSentry = ".build/local-sentry/Sentry.xcframework"
let localSentryURL = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent(localSentry)
let sentryTarget: Target = FileManager.default.fileExists(atPath: localSentryURL.path)
    ? .binaryTarget(name: "Sentry", path: localSentry)
    : .binaryTarget(name: "Sentry",
        url: "https://github.com/getsentry/sentry-cocoa/releases/download/9.30.0/Sentry.xcframework.zip",
        checksum: "46f659ad81a4a53db82f263d5ce5a3d704e6b7f6625518b175a4d1b630cd90a9")

let package = Package(
    name: "steamac-vm",
    platforms: [.macOS(.v15)],
    dependencies: [],
    targets: [
        sentryTarget,
        .systemLibrary(name: "CKrun", path: "Sources/CKrun"),
        // zstd decoder (sources fetched by fetch-zstd.sh into Sources/CZstd/zstd, included by czstd.c).
        .target(name: "CZstd", path: "Sources/CZstd", exclude: ["zstd"]),
        .executableTarget(
            name: "steamac-vm",
            dependencies: ["CKrun", "CZstd", "Sentry"],
            path: "Sources/steamac-vm",
            linkerSettings: [
                .linkedLibrary("c++"),
                .linkedFramework("AppKit"),
                .linkedFramework("Metal"),
                .linkedFramework("QuartzCore"),
                .linkedFramework("GameController"),
                .linkedFramework("ImageIO"),
                .linkedFramework("Carbon"),
                .linkedFramework("CoreAudio"),
                .linkedFramework("SwiftUI"),
                .linkedFramework("Security"),
            ]
        ),
    ],
    swiftLanguageModes: [.v5]
)
