// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "WhisperKey",
    platforms: [
        .macOS(.v14)
    ],
    products: [
        .executable(name: "WhisperKey", targets: ["WhisperKey"]),
    ],
    targets: [
        .target(
            name: "CWhisper",
            path: "Sources/CWhisper",
            publicHeadersPath: "include",
            cSettings: [
                .unsafeFlags(["-I/opt/homebrew/include"])
            ],
            linkerSettings: [
                .unsafeFlags(["-L/opt/homebrew/lib"]),
                .linkedLibrary("whisper"),
                .linkedLibrary("ggml")
            ]
        ),
        .executableTarget(
            name: "WhisperKey",
            dependencies: ["CWhisper"],
            path: "Sources/WhisperKey",
            linkerSettings: [
                .linkedLibrary("whisper")
            ]
        )
    ]
)
