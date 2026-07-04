import AppKit

final class ModelManager {
    private let fileManager = FileManager.default
    private let modelDirectoryKey = "WhisperKeyModelDirectory"
    var activeModelName: String?
    private(set) var localModels: [ModelInfo] = []

    private let allModels: [ModelManifest] = [
        ModelManifest(name: "Tiny", url: URL(string: "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin")!),
        ModelManifest(name: "Small", url: URL(string: "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin")!),
        ModelManifest(name: "Medium", url: URL(string: "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin")!),
        ModelManifest(name: "Large-v3", url: URL(string: "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3.bin")!),
    ]

    var availableModels: [ModelManifest] {
        let downloadedNames = Set(localModels.map { $0.name })
        return allModels.filter { !downloadedNames.contains($0.name) }
    }

    init() {
        Logger.info("[ModelManager] Initializing model manager")
        refreshLocalModels()
    }

    func restoreModelsDirectory() {
        Logger.info("[ModelManager] Restoring models directory")
        if let bookmark = UserDefaults.standard.data(forKey: modelDirectoryKey),
           let url = resolveBookmark(bookmark) {
            Logger.debug("[ModelManager] Restored bookmarked models directory: \(url.path)")
            updateLocalModelsDirectory(url)
        } else {
            let defaultURL = defaultModelsDirectory()
            Logger.debug("[ModelManager] Using default models directory: \(defaultURL.path)")
            updateLocalModelsDirectory(defaultURL)
        }
    }

    func activeModelURL() -> URL? {
        if let name = activeModelName {
            return localModels.first(where: { $0.name == name })?.path
        }
        return localModels.first?.path
    }

    func chooseModelsDirectory(owner: NSWindow?, completion: @escaping (Bool) -> Void) {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Choose"
        panel.beginSheetModal(for: owner ?? NSApp.mainWindow ?? NSApp.windows.first ?? NSApp.mainWindow!) { [weak self] response in
            guard response == .OK, let url = panel.url else {
                completion(false)
                return
            }
            self?.storeBookmark(url: url)
            self?.updateLocalModelsDirectory(url)
            completion(true)
        }
    }

    func downloadModel(_ manifest: ModelManifest, progress: @escaping (Double) -> Void, completion: @escaping (Result<Void, Error>) -> Void) {
        guard let targetDir = currentModelsDirectory() else {
            Logger.error("[ModelManager] Model directory not configured for download")
            completion(.failure(NSError(domain: "WhisperKey", code: -1, userInfo: [NSLocalizedDescriptionKey: "Model directory not configured"])))
            return
        }

        let destination = targetDir.appendingPathComponent(manifest.name + ".bin")
        Logger.info("[ModelManager] Downloading model \(manifest.name) to \(destination.path)")
        let task = URLSession.shared.downloadTask(with: manifest.url) { url, _, error in
            if let error {
                completion(.failure(error))
                return
            }
            guard let url else {
                completion(.failure(NSError(domain: "WhisperKey", code: -2, userInfo: [NSLocalizedDescriptionKey: "Download returned no data"])))
                return
            }
            do {
                try self.fileManager.createDirectory(at: targetDir, withIntermediateDirectories: true)
                try self.fileManager.moveItem(at: url, to: destination)
                self.refreshLocalModels()
                completion(.success(()))
            } catch {
                completion(.failure(error))
            }
        }
        task.resume()
    }

    private func refreshLocalModels() {
        guard let directory = currentModelsDirectory() else { return }
        Logger.info("[ModelManager] Refreshing local models from \(directory.path)")
        do {
            let contents = try fileManager.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil)
            localModels = contents.filter { $0.pathExtension.lowercased() == "bin" }
                .map { url in
                    ModelInfo(name: url.deletingPathExtension().lastPathComponent, path: url)
                }
                .sorted(by: { $0.name.localizedStandardCompare($1.name) == .orderedAscending })

            Logger.debug("[ModelManager] Found local models: \(localModels.map { $0.name })")

            if let activeName = activeModelName,
               localModels.contains(where: { $0.name == activeName }) {
                return
            }

            activeModelName = localModels.first?.name
            Logger.debug("[ModelManager] Active model set to: \(activeModelName ?? "none")")
        } catch {
            Logger.error("[ModelManager] Failed to refresh local models: \(error.localizedDescription)")
            localModels = []
            activeModelName = nil
        }
    }

    private func updateLocalModelsDirectory(_ url: URL) {
        currentModelDirectory = url
        refreshLocalModels()
    }

    private func defaultModelsDirectory() -> URL {
        let brewDirectory = URL(fileURLWithPath: "/opt/homebrew/share/whisper-cpp/models")
        if fileManager.fileExists(atPath: brewDirectory.path) {
            return brewDirectory
        }

        let directory = fileManager.homeDirectoryForCurrentUser.appendingPathComponent("WhisperKeyModels")
        if !fileManager.fileExists(atPath: directory.path) {
            try? fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
        }
        return directory
    }

    private func currentModelsDirectory() -> URL? {
        currentModelDirectory
    }

    private var currentModelDirectory: URL?

    private func storeBookmark(url: URL) {
        do {
            let bookmark = try url.bookmarkData(options: [.withSecurityScope], includingResourceValuesForKeys: nil, relativeTo: nil)
            UserDefaults.standard.set(bookmark, forKey: modelDirectoryKey)
            Logger.debug("[ModelManager] Stored bookmark for models directory: \(url.path)")
        } catch {
            Logger.error("[ModelManager] Bookmark creation failed: \(error.localizedDescription)")
        }
    }

    private func resolveBookmark(_ data: Data) -> URL? {
        var stale = false
        do {
            let url = try URL(resolvingBookmarkData: data, options: [.withSecurityScope], relativeTo: nil, bookmarkDataIsStale: &stale)
            return url
        } catch {
            return nil
        }
    }
}
