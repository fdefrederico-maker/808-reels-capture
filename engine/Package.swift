// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "ReelsEngine",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "ReelsEngine",
            path: "Sources/ReelsEngine",
            linkerSettings: [
                .linkedFramework("ScreenCaptureKit"),
                .linkedFramework("AVFoundation"),
                .linkedFramework("CoreImage"),
                .linkedFramework("CoreMedia"),
                .linkedFramework("AppKit"),
            ]
        )
    ],
    swiftLanguageVersions: [.v5]
)
