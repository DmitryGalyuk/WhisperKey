import AppKit

final class OverlayWindow: NSObject {
    private let window: NSWindow
    private let label: NSTextField

    override init() {
        let styleMask: NSWindow.StyleMask = [.borderless]
        let frame = NSRect(x: 0, y: 0, width: 320, height: 60)
        window = NSWindow(contentRect: frame, styleMask: styleMask, backing: .buffered, defer: false)
        window.isOpaque = false
        window.backgroundColor = NSColor.black.withAlphaComponent(0.55)
        window.level = .statusBar
        window.hasShadow = true
        window.collectionBehavior = [.canJoinAllSpaces, .transient, .ignoresCycle]

        label = NSTextField(labelWithString: "")
        label.alignment = .center
        label.textColor = .white
        label.font = .systemFont(ofSize: 18, weight: .semibold)
        label.translatesAutoresizingMaskIntoConstraints = false

        super.init()

        window.contentView?.addSubview(label)
        NSLayoutConstraint.activate([
            label.centerXAnchor.constraint(equalTo: window.contentView!.centerXAnchor),
            label.centerYAnchor.constraint(equalTo: window.contentView!.centerYAnchor),
            label.leadingAnchor.constraint(equalTo: window.contentView!.leadingAnchor, constant: 16),
            label.trailingAnchor.constraint(equalTo: window.contentView!.trailingAnchor, constant: -16),
        ])
    }

    func show(state: String) {
        label.stringValue = state
        if !window.isVisible {
            positionWindow()
            window.orderFrontRegardless()
        }
        NSObject.cancelPreviousPerformRequests(withTarget: self)
        perform(#selector(hide), with: nil, afterDelay: 1.5)
    }

    @objc private func hide() {
        window.orderOut(nil)
    }

    private func positionWindow() {
        guard let screen = NSScreen.main else { return }
        let width: CGFloat = 340
        let height: CGFloat = 56
        let x = screen.frame.midX - width / 2
        let y = screen.frame.maxY - height - 120
        window.setFrame(NSRect(x: x, y: y, width: width, height: height), display: true)
    }
}
