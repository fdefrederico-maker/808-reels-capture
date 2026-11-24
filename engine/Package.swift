// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "C00KUPEngine",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "C00KUPEngine",
            path: "Sources/C00KUPEngine",
            linkerSettings: [
                .linkedFramework("ScreenCaptureKit"),
                .linkedFramework("AVFoundation"),
                .linkedFramework("CoreImage"),
                .linkedFramework("CoreMedia"),
                .linkedFramework("AppKit"),
                .linkedFramework("CoreAudio"),
            ]
        )
    ],
    swiftLanguageVersions: [.v5]
)
