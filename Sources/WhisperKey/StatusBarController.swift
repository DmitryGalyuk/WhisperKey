import AppKit

final class StatusBarController {
    weak var delegate: StatusBarControllerDelegate?
    private var statusItem: NSStatusItem
    var audioDeviceManager: AudioDeviceManager?
    var modelManager: ModelManager?
    private var menu = NSMenu()

    init() {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = statusItem.button {
            button.image = NSImage(systemSymbolName: "mic", accessibilityDescription: "WhisperKey")
            button.image?.isTemplate = true
        }
    }

    func buildMenu() {
        rebuildMenu()
    }

    func rebuildMenu() {
        menu.removeAllItems()

        let micTitle = NSMenuItem(title: "Microphone", action: nil, keyEquivalent: "")
        micTitle.isEnabled = false
        menu.addItem(micTitle)

        if let devices = audioDeviceManager?.devices {
            devices.forEach { device in
                let item = NSMenuItem(title: device.name, action: #selector(selectDevice(_:)), keyEquivalent: "")
                item.target = self
                item.representedObject = device
                item.state = audioDeviceManager?.selectedDeviceID == device.id ? .on : .off
                menu.addItem(item)
            }
        }
        menu.addItem(NSMenuItem.separator())

        let modelTitle = NSMenuItem(title: "Active Model", action: nil, keyEquivalent: "")
        modelTitle.isEnabled = false
        menu.addItem(modelTitle)

        if let modelManager {
            let activeName = modelManager.activeModelName ?? "None"
            menu.addItem(NSMenuItem(title: "Current: \(activeName)", action: nil, keyEquivalent: ""))
            menu.addItem(NSMenuItem.separator())

            menu.addItem(NSMenuItem(title: "Local Models", action: nil, keyEquivalent: ""))
            modelManager.localModels.forEach { model in
                let item = NSMenuItem(title: model.name, action: #selector(selectModel(_:)), keyEquivalent: "")
                item.target = self
                item.representedObject = model
                item.state = modelManager.activeModelName == model.name ? .on : .off
                menu.addItem(item)
            }

            menu.addItem(NSMenuItem.separator())
            let availableTitle = NSMenuItem(title: "Download Models", action: nil, keyEquivalent: "")
            availableTitle.isEnabled = false
            menu.addItem(availableTitle)
            modelManager.availableModels.forEach { manifest in
                let item = NSMenuItem(title: manifest.name, action: #selector(downloadModel(_:)), keyEquivalent: "")
                item.target = self
                item.representedObject = manifest
                menu.addItem(item)
            }
        }

        menu.addItem(NSMenuItem.separator())
        let settingsItem = NSMenuItem(title: "Settings…", action: #selector(openSettings), keyEquivalent: "")
        settingsItem.target = self
        menu.addItem(settingsItem)

        let quitItem = NSMenuItem(title: "Quit", action: #selector(quitApp), keyEquivalent: "q")
        quitItem.target = self
        menu.addItem(quitItem)

        statusItem.menu = menu
    }

    func update(state: AppState) {
        if let button = statusItem.button {
            button.title = state == .recording ? "🎙️" : ""
        }
    }

    func updateDownloadProgress(_ modelName: String, progress: Double) {
        if let item = menu.items.first(where: { $0.title == modelName }) {
            item.title = "\(modelName) — \(Int(progress * 100))%"
        }
    }

    @objc private func selectDevice(_ sender: NSMenuItem) {
        guard let device = sender.representedObject as? AudioDevice else { return }
        delegate?.statusBarController(self, selectedDevice: device)
    }

    @objc private func selectModel(_ sender: NSMenuItem) {
        guard let model = sender.representedObject as? ModelInfo else { return }
        delegate?.statusBarController(self, selectedModel: model)
    }

    @objc private func downloadModel(_ sender: NSMenuItem) {
        guard let manifest = sender.representedObject as? ModelManifest else { return }
        delegate?.statusBarController(self, requestedDownload: manifest)
    }

    @objc private func openSettings() {
        delegate?.statusBarControllerDidRequestSettings(self)
    }

    @objc private func quitApp() {
        NSApplication.shared.terminate(nil)
    }
}
