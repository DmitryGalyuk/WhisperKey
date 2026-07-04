import AVFoundation

struct AudioDevice {
    let id: String
    let name: String
}

enum AudioCaptureError: Error {
    case microphonePermissionDenied
    case cannotCreateSession
    case cannotCreateInput
    case cannotCreateOutput
    case captureFailed
}

final class AudioDeviceManager {
    private(set) var devices: [AudioDevice] = []
    private(set) var selectedDeviceID: String?

    func refreshDevices() {
        let discoverySession = AVCaptureDevice.DiscoverySession(deviceTypes: [.builtInMicrophone, .externalUnknown], mediaType: .audio, position: .unspecified)
        let inputs = discoverySession.devices
        devices = inputs.map { device in
            AudioDevice(id: device.uniqueID, name: device.localizedName)
        }

        if let selectedDeviceID = selectedDeviceID,
           devices.contains(where: { $0.id == selectedDeviceID }) {
            return
        }

        if let defaultDevice = AVCaptureDevice.default(for: .audio),
           devices.contains(where: { $0.id == defaultDevice.uniqueID }) {
            self.selectedDeviceID = defaultDevice.uniqueID
            return
        }

        if let first = devices.first {
            selectedDeviceID = first.id
        }
    }

    func select(device: AudioDevice) throws {
        guard devices.contains(where: { $0.id == device.id }) else {
            throw AudioCaptureError.captureFailed
        }
        selectedDeviceID = device.id
    }
}


final class AudioCaptureManager: NSObject {
    private let session = AVCaptureSession()
    private let audioOutput = AVCaptureAudioDataOutput()
    private var sampleBufferQueue = [Float]()
    private var audioQueue = DispatchQueue(label: "WhisperKey.AudioCapture")
    private let audioDeviceManager: AudioDeviceManager
    private let targetSampleRate: Double = 16000
    private let targetChannelCount = 1

    private var selectedDeviceID: String? {
        audioDeviceManager.selectedDeviceID
    }

    init(deviceManager: AudioDeviceManager) {
        self.audioDeviceManager = deviceManager
        super.init()
    }

    func requestMicrophoneAccess(completion: @escaping (Bool) -> Void) {
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .authorized:
            Logger.debug("[AudioCaptureManager] Microphone authorization status: authorized")
            completion(true)
        case .notDetermined:
            Logger.debug("[AudioCaptureManager] Microphone authorization status: notDetermined")
            AVCaptureDevice.requestAccess(for: .audio) { granted in
                Logger.info("[AudioCaptureManager] Microphone access request result: \(granted)")
                completion(granted)
            }
        default:
            Logger.error("[AudioCaptureManager] Microphone authorization denied or restricted")
            completion(false)
        }
    }

    func startCapture() throws {
        guard AVCaptureDevice.authorizationStatus(for: .audio) == .authorized else {
            Logger.error("[AudioCaptureManager] Microphone capture start failed: not authorized")
            throw AudioCaptureError.microphonePermissionDenied
        }

        session.beginConfiguration()
        session.sessionPreset = .high

        if let currentInputs = session.inputs as? [AVCaptureDeviceInput] {
            for input in currentInputs {
                session.removeInput(input)
            }
        }

        if !session.outputs.isEmpty {
            for output in session.outputs {
                session.removeOutput(output)
            }
        }

        let selectedID = selectedDeviceID
        let defaultDevice = AVCaptureDevice.default(for: .audio)
        let uniqueID = selectedID ?? defaultDevice?.uniqueID
        guard let id = uniqueID, let device = AVCaptureDevice(uniqueID: id) else {
            throw AudioCaptureError.cannotCreateInput
        }

        let audioInput = try AVCaptureDeviceInput(device: device)
        if session.canAddInput(audioInput) {
            session.addInput(audioInput)
        } else {
            throw AudioCaptureError.cannotCreateInput
        }

        audioOutput.audioSettings = [
            AVFormatIDKey: kAudioFormatLinearPCM,
            AVSampleRateKey: targetSampleRate,
            AVNumberOfChannelsKey: targetChannelCount,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsNonInterleaved: false,
            AVLinearPCMIsBigEndianKey: false,
        ]
        audioOutput.setSampleBufferDelegate(self, queue: audioQueue)
        if session.canAddOutput(audioOutput) {
            session.addOutput(audioOutput)
        } else {
            Logger.error("[AudioCaptureManager] Unable to add audio output to capture session")
            throw AudioCaptureError.cannotCreateOutput
        }

        sampleBufferQueue.removeAll()
        session.commitConfiguration()
        session.startRunning()
        Logger.info("[AudioCaptureManager] Started capture using device ID: \(selectedDeviceID ?? "default")")
    }

    func stopCapture() -> [Float] {
        session.stopRunning()
        Logger.info("[AudioCaptureManager] Stopped capture; samples captured: \(sampleBufferQueue.count)")
        saveDebugWav(samples: sampleBufferQueue)
        return sampleBufferQueue
    }
}

extension AudioCaptureManager: AVCaptureAudioDataOutputSampleBufferDelegate {
    func captureOutput(_ output: AVCaptureOutput, didOutput sampleBuffer: CMSampleBuffer, from connection: AVCaptureConnection) {
        guard let formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer),
              let asbdPointer = CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc) else {
            return
        }

        let asbd = asbdPointer.pointee
        let sampleCount = CMSampleBufferGetNumSamples(sampleBuffer)
        guard sampleCount > 0 else { return }

        guard let blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer) else { return }
        var length = 0
        var dataPointer: UnsafeMutablePointer<Int8>?
        guard CMBlockBufferGetDataPointer(blockBuffer, atOffset: 0, lengthAtOffsetOut: nil, totalLengthOut: &length, dataPointerOut: &dataPointer) == noErr,
              let rawPointer = dataPointer else {
            return
        }

        if asbd.mFormatID == kAudioFormatLinearPCM {
            let bytesPerSample = Int(asbd.mBitsPerChannel / 8)
            let frameCount = length / Int(asbd.mBytesPerFrame)
            let channelCount = Int(asbd.mChannelsPerFrame)

            if (asbd.mFormatFlags & kAudioFormatFlagIsFloat) != 0 && asbd.mBitsPerChannel == 32 {
                rawPointer.withMemoryRebound(to: Float.self, capacity: frameCount * channelCount) { buffer in
                    for index in 0..<(frameCount * channelCount) {
                        sampleBufferQueue.append(buffer[index])
                    }
                }
            } else if (asbd.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0 && bytesPerSample == 2 {
                rawPointer.withMemoryRebound(to: Int16.self, capacity: frameCount * channelCount) { buffer in
                    for index in 0..<(frameCount * channelCount) {
                        sampleBufferQueue.append(Float(buffer[index]) / Float(Int16.max))
                    }
                }
            } else if (asbd.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0 && bytesPerSample == 4 {
                rawPointer.withMemoryRebound(to: Int32.self, capacity: frameCount * channelCount) { buffer in
                    let maxValue = Float(Int32.max)
                    for index in 0..<(frameCount * channelCount) {
                        sampleBufferQueue.append(Float(buffer[index]) / maxValue)
                    }
                }
            } else {
                Logger.error("[AudioCaptureManager] Unsupported PCM format: flags=\(asbd.mFormatFlags), bits=\(asbd.mBitsPerChannel), channels=\(asbd.mChannelsPerFrame)")
            }
        }
    }

    private func saveDebugWav(samples: [Float]) {
        guard !samples.isEmpty else { return }
        let directory = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Logs/WhisperKey")
        try? FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let timestamp = Int(Date().timeIntervalSince1970)
    #if DEBUG
        let buildTag = "Debug"
    #else
        let buildTag = "Release"
    #endif
        let url = directory.appendingPathComponent("WhisperKey_LastCapture_\(buildTag)_\(timestamp).wav")

        let clippedSamples: [Int16] = samples.map { sample in
            let clipped = max(-1.0, min(1.0, sample))
            return Int16(clipped * Float(Int16.max))
        }
        let sampleData = clippedSamples.withUnsafeBufferPointer { buffer in
            Data(buffer: buffer)
        }

        var header = Data()
        header.append("RIFF".data(using: .ascii)!)
        header.append(UInt32(36 + sampleData.count).littleEndianData)
        header.append("WAVE".data(using: .ascii)!)
        header.append("fmt " .data(using: .ascii)!)
        header.append(UInt32(16).littleEndianData)
        header.append(UInt16(1).littleEndianData)
        header.append(UInt16(targetChannelCount).littleEndianData)
        header.append(UInt32(UInt32(targetSampleRate)).littleEndianData)
        header.append(UInt32(UInt32(targetSampleRate) * UInt32(targetChannelCount) * UInt32(MemoryLayout<Int16>.size)).littleEndianData)
        header.append(UInt16(UInt16(targetChannelCount) * UInt16(MemoryLayout<Int16>.size)).littleEndianData)
        header.append(UInt16(16).littleEndianData)
        header.append("data".data(using: .ascii)!)
        header.append(UInt32(sampleData.count).littleEndianData)

        var wavFile = header
        wavFile.append(sampleData)
        do {
            try wavFile.write(to: url)
            Logger.info("[AudioCaptureManager] Saved debug WAV at \(url.path)")
        } catch {
            Logger.error("[AudioCaptureManager] Failed to save debug WAV: \(error.localizedDescription)")
        }
    }
}

private extension FixedWidthInteger {
    var littleEndianData: Data {
        withUnsafeBytes(of: self.littleEndian) { Data($0) }
    }
}
