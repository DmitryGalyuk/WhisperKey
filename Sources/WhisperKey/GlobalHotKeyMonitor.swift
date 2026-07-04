import Cocoa

final class GlobalHotKeyMonitor {
    weak var delegate: HotkeyMonitorDelegate?
    private var eventTap: CFMachPort?
    private var runLoopSource: CFRunLoopSource?
    private var keyDownMonitor: Any?
    private var keyUpMonitor: Any?
    private let hotkeyCodes: Set<CGKeyCode> = [176] // Microphone button keycode
    private var isDown = false

    func start() -> Bool {
        Logger.info("[GlobalHotKeyMonitor] Starting event tap")
        let success = createEventTap()
        if !success {
            Logger.error("[GlobalHotKeyMonitor] Failed to create event tap")
        }
        return success
    }

    private func createEventTap() -> Bool {
        let mask: CGEventMask = (1 << CGEventType.keyDown.rawValue) | (1 << CGEventType.keyUp.rawValue)
        eventTap = CGEvent.tapCreate(tap: .cghidEventTap,
                                     place: .headInsertEventTap,
                                     options: .defaultTap,
                                     eventsOfInterest: mask,
                                     callback: { proxy, type, event, refcon in
            guard let refcon else { return Unmanaged.passUnretained(event) }
            let monitor = Unmanaged<GlobalHotKeyMonitor>.fromOpaque(refcon).takeUnretainedValue()
            return monitor.handleEvent(proxy: proxy, type: type, event: event)
        }, userInfo: UnsafeMutableRawPointer(Unmanaged.passUnretained(self).toOpaque()))

        guard let eventTap else { return false }
        runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0)
        CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, .commonModes)
        CGEvent.tapEnable(tap: eventTap, enable: true)
        return true
    }

    func stop() {
        if let runLoopSource = runLoopSource {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), runLoopSource, .commonModes)
        }
        if let eventTap = eventTap {
            CFMachPortInvalidate(eventTap)
        }
        if let keyDownMonitor = keyDownMonitor {
            NSEvent.removeMonitor(keyDownMonitor)
        }
        if let keyUpMonitor = keyUpMonitor {
            NSEvent.removeMonitor(keyUpMonitor)
        }
        self.eventTap = nil
        self.runLoopSource = nil
        self.keyDownMonitor = nil
        self.keyUpMonitor = nil
    }

    private func handleEvent(proxy: CGEventTapProxy, type: CGEventType, event: CGEvent) -> Unmanaged<CGEvent>? {
        if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
            Logger.error("[GlobalHotKeyMonitor] Event tap disabled by timeout or user input")
            if let eventTap {
                CGEvent.tapEnable(tap: eventTap, enable: true)
                Logger.info("[GlobalHotKeyMonitor] Re-enabled event tap")
            }
            return Unmanaged.passUnretained(event)
        }

        let keycode = CGKeyCode(event.getIntegerValueField(.keyboardEventKeycode))
        if type == .keyDown || type == .keyUp {
            if hotkeyCodes.contains(keycode) {
                handleKeyEvent(type: type, keycode: keycode)
                return nil
            }
        }
        return Unmanaged.passUnretained(event)
    }

    private func handleKeyEvent(type: CGEventType, keycode: CGKeyCode) {
        switch (type, keycode) {
        case (.keyDown, let code) where hotkeyCodes.contains(code):
            if !isDown {
                isDown = true
                delegate?.hotkeyMonitorDidPressButton(self)
            }
        case (.keyUp, let code) where hotkeyCodes.contains(code):
            isDown = false
            delegate?.hotkeyMonitorDidReleaseButton(self)
        default:
            break
        }
    }
}

