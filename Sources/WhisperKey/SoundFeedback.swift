import Foundation

enum SoundFeedback {
    static func playStart() {
        play(sound: "/System/Library/Sounds/Tink.aiff")
    }

    static func playProcessing() {
        play(sound: "/System/Library/Sounds/Pop.aiff")
    }

    static func playDone() {
        play(sound: "/System/Library/Sounds/Glass.aiff")
    }

    private static func play(sound path: String) {
        let process = Process()
        process.launchPath = "/usr/bin/afplay"
        process.arguments = [path]
        try? process.run()
    }
}
