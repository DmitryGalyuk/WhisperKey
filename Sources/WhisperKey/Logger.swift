import Foundation

struct Logger {
    private static let queue = DispatchQueue(label: "WhisperKey.Logger")
    private static let logDirectory: URL = {
        let url = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Logs/WhisperKey")
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }()

    private static let logFileURL: URL = logDirectory.appendingPathComponent("WhisperKey.log")

    static func log(_ message: String, level: String = "INFO") {
        let timestamp = ISO8601DateFormatter().string(from: Date())
        let line = "[\(timestamp)] [\(level)] \(message)\n"
        queue.async {
            print(line, terminator: "")
            appendToFile(line)
        }
    }

    static func debug(_ message: String) {
        log(message, level: "DEBUG")
    }

    static func info(_ message: String) {
        log(message, level: "INFO")
    }

    static func error(_ message: String) {
        log(message, level: "ERROR")
    }

    private static func appendToFile(_ line: String) {
        guard let data = line.data(using: .utf8) else { return }
        if !FileManager.default.fileExists(atPath: logFileURL.path) {
            FileManager.default.createFile(atPath: logFileURL.path, contents: data, attributes: nil)
            return
        }

        guard let handle = try? FileHandle(forWritingTo: logFileURL) else { return }
        defer { try? handle.close() }
        handle.seekToEndOfFile()
        handle.write(data)
    }
}
