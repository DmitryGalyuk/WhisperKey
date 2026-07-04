import Foundation
import ApplicationServices

enum KeyboardInjector {
    static func pasteFromClipboard() {
        guard let source = CGEventSource(stateID: .hidSystemState) else { return }
        let vDown = CGEvent(keyboardEventSource: source, virtualKey: 9, keyDown: true)
        let vUp = CGEvent(keyboardEventSource: source, virtualKey: 9, keyDown: false)
        vDown?.flags = .maskCommand
        vUp?.flags = .maskCommand
        vDown?.post(tap: .cghidEventTap)
        vUp?.post(tap: .cghidEventTap)
    }
}
