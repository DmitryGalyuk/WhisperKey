import Foundation

final class CooldownController {
    private var timer: Timer?

    func start(duration: TimeInterval, completion: @escaping () -> Void) {
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: duration, repeats: false) { _ in
            completion()
            self.timer = nil
        }
    }

    func cancel() {
        timer?.invalidate()
        timer = nil
    }
}
