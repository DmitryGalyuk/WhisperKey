import AppKit
import AVFoundation
import ApplicationServices

enum AppState {
    case idle
    case loading
    case recording
    case transcribing
    case pasting
    case cooldown

    var title: String {
        switch self {
        case .idle: return "Idle"
        case .loading: return "Loading model…"
        case .recording: return "🎙️ Recording"
        case .transcribing: return "⏳ Transcribing"
        case .pasting: return "✅ Sending"
        case .cooldown: return "Cooldown"
        }
    }
}

protocol StatusBarControllerDelegate: AnyObject {
    func statusBarController(_ controller: StatusBarController, selectedDevice device: AudioDevice)
    func statusBarController(_ controller: StatusBarController, selectedModel model: ModelInfo)
    func statusBarController(_ controller: StatusBarController, requestedDownload manifest: ModelManifest)
    func statusBarControllerDidRequestSettings(_ controller: StatusBarController)
}

protocol HotkeyMonitorDelegate: AnyObject {
    func hotkeyMonitorDidPressButton(_ monitor: GlobalHotKeyMonitor)
    func hotkeyMonitorDidReleaseButton(_ monitor: GlobalHotKeyMonitor)
}

class AppDelegate: NSObject, NSApplicationDelegate, StatusBarControllerDelegate, HotkeyMonitorDelegate {
    private let audioDeviceManager = AudioDeviceManager()
    private lazy var audioCapture = AudioCaptureManager(deviceManager: audioDeviceManager)
    private let modelManager = ModelManager()
    private let whisperEngine = WhisperEngine()
    private let overlay = OverlayWindow()
    private let statusBar = StatusBarController()
    private let hotkeyMonitor = GlobalHotKeyMonitor()
    private let cooldownController = CooldownController()

    private var currentState: AppState = .idle {
        didSet {
            overlay.show(state: currentState.title)
            statusBar.update(state: currentState)
        }
    }

    private var shouldRecordAfterLoad = false
    private var micButtonDown = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        Logger.info("[AppDelegate] Application finished launching")
        #if DEBUG
        Logger.info("[AppDelegate] Build configuration: Debug")
        #else
        Logger.info("[AppDelegate] Build configuration: Release")
        #endif
        statusBar.delegate = self
        statusBar.audioDeviceManager = audioDeviceManager
        statusBar.modelManager = modelManager
        statusBar.buildMenu()

        modelManager.restoreModelsDirectory()
        audioDeviceManager.refreshDevices()
        audioCapture.requestMicrophoneAccess { [weak self] granted in
            DispatchQueue.main.async {
                Logger.info("[AppDelegate] Microphone permission granted: \(granted)")
                if !granted {
                    self?.showSecurityAlert("Microphone access is required for WhisperKey.")
                }
            }
        }

        requestAccessibilityPermissionIfNeeded()

        hotkeyMonitor.delegate = self
        if !hotkeyMonitor.start() {
            Logger.error("[AppDelegate] Failed to start hotkey monitor")
            showSecurityAlert("WhisperKey could not register the global hotkey monitor. Please enable Input Monitoring or Accessibility permission for WhisperKey in System Settings > Privacy & Security.")
        }

        updateMenuAsync()
        overlay.show(state: currentState.title)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return false
    }

    private func showSecurityAlert(_ message: String) {
        let alert = NSAlert()
        alert.messageText = "WhisperKey needs permission"
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")
        alert.runModal()
    }

    func statusBarController(_ controller: StatusBarController, selectedDevice device: AudioDevice) {
        Logger.info("[AppDelegate] User selected audio device: \(device.name)")
        do {
            try audioDeviceManager.select(device: device)
            audioDeviceManager.refreshDevices()
            updateMenuAsync()
        } catch {
            Logger.error("[AppDelegate] Unable to switch input device: \(error.localizedDescription)")
            showSecurityAlert("Unable to switch input device: \(error.localizedDescription)")
        }
    }

    func statusBarController(_ controller: StatusBarController, selectedModel model: ModelInfo) {
        Logger.info("[AppDelegate] User selected model: \(model.name)")
        modelManager.activeModelName = model.name
        updateMenuAsync()
        whisperEngine.unloadModel()
        currentState = .idle
    }

    func statusBarController(_ controller: StatusBarController, requestedDownload manifest: ModelManifest) {
        modelManager.downloadModel(manifest) { [weak self] progress in
            DispatchQueue.main.async {
                self?.statusBar.updateDownloadProgress(manifest.name, progress: progress)
            }
        } completion: { [weak self] result in
            DispatchQueue.main.async {
                switch result {
                case .success:
                    self?.audioDeviceManager.refreshDevices()
                    self?.updateMenuAsync()
                case .failure(let error):
                    self?.showSecurityAlert("Model download failed: \(error.localizedDescription)")
                }
            }
        }
    }

    func statusBarControllerDidRequestSettings(_ controller: StatusBarController) {
        modelManager.chooseModelsDirectory(owner: NSApp.mainWindow) { [weak self] success in
            DispatchQueue.main.async {
                if success {
                    self?.updateMenuAsync()
                }
            }
        }
    }

    private func requestAccessibilityPermissionIfNeeded() {
        let options = [kAXTrustedCheckOptionPrompt.takeRetainedValue() as String: true] as CFDictionary
        let trusted = AXIsProcessTrustedWithOptions(options)
        Logger.info("[AppDelegate] Accessibility trust status: \(trusted)")
        if !trusted {
            showSecurityAlert("WhisperKey needs Accessibility / Input Monitoring permission to detect the global hotkey. Please allow WhisperKey in System Settings > Privacy & Security > Input Monitoring.")
        }
    }

    func hotkeyMonitorDidPressButton(_ monitor: GlobalHotKeyMonitor) {
        Logger.info("[AppDelegate] Hotkey pressed")
        guard !micButtonDown else { return }
        micButtonDown = true

        cooldownController.cancel()
        switch currentState {
        case .idle, .cooldown, .pasting:
            if whisperEngine.isLoaded {
                startRecording()
            } else {
                loadModelAndRecord()
            }
        case .recording:
            stopRecording()
        case .loading:
            Logger.debug("[AppDelegate] Hotkey pressed while model is loading; waiting for load to finish")
        case .transcribing:
            Logger.debug("[AppDelegate] Hotkey pressed while in state: \(currentState)")
        }
    }

    func hotkeyMonitorDidReleaseButton(_ monitor: GlobalHotKeyMonitor) {
        Logger.info("[AppDelegate] Hotkey released")
        guard micButtonDown else { return }
        micButtonDown = false
        // Do not cancel loading or recording on release. The hotkey state should
        // toggle on press, so release is only used to track the button state.
    }

    private func loadModelAndRecord() {
        Logger.info("[AppDelegate] Loading model and preparing to record")
        guard let modelURL = modelManager.activeModelURL() else {
            Logger.error("[AppDelegate] No active model URL available")
            showSecurityAlert("No Whisper model is available. Please download or place a .bin model in the models directory.")
            return
        }

        currentState = .loading
        shouldRecordAfterLoad = true

        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            do {
                try self?.whisperEngine.loadModel(at: modelURL)
                DispatchQueue.main.async {
                    guard let self else { return }
                    if self.shouldRecordAfterLoad {
                        self.startRecording()
                    } else {
                        self.currentState = .idle
                    }
                }
            } catch {
                Logger.error("[AppDelegate] Model load failed: \(error.localizedDescription)")
                DispatchQueue.main.async {
                    self?.currentState = .idle
                    self?.showSecurityAlert("Whisper model load failed: \(error.localizedDescription)")
                }
            }
        }
    }

    private func startRecording() {
        Logger.info("[AppDelegate] Starting audio recording")
        do {
            try audioCapture.startCapture()
            currentState = .recording
            SoundFeedback.playStart()
        } catch {
            Logger.error("[AppDelegate] Microphone capture failed: \(error.localizedDescription)")
            currentState = .idle
            showSecurityAlert("Microphone capture failed: \(error.localizedDescription)")
        }
    }

    private func stopRecording() {
        Logger.info("[AppDelegate] Stopping recording and starting transcription")
        currentState = .transcribing
        SoundFeedback.playProcessing()

        let samples = audioCapture.stopCapture()
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }
            do {
                let text = try self.whisperEngine.transcribe(samples: samples)
                Logger.info("[AppDelegate] Transcription completed; characters: \(text.count)")
                DispatchQueue.main.async {
                    self.insertText(text)
                }
            } catch {
                Logger.error("[AppDelegate] Transcription failed: \(error.localizedDescription)")
                DispatchQueue.main.async {
                    self.currentState = .idle
                    self.showSecurityAlert("Transcription failed: \(error.localizedDescription)")
                }
            }
        }
    }

    private func insertText(_ text: String) {
        Logger.info("[AppDelegate] Inserting text into clipboard/pasteboard")
        guard !text.isEmpty else {
            currentState = .idle
            return
        }

        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
        currentState = .pasting
        KeyboardInjector.pasteFromClipboard()
        SoundFeedback.playDone()

        cooldownController.start(duration: 1800) { [weak self] in
            self?.whisperEngine.unloadModel()
            self?.currentState = .idle
        }
    }

    private func updateMenuAsync() {
        DispatchQueue.main.async { [weak self] in
            self?.statusBar.rebuildMenu()
        }
    }
}
