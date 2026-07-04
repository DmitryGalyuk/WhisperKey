import Foundation
import CWhisper

enum WhisperError: Error {
    case libraryUnavailable
    case modelLoadFailed
    case transcriptionFailed(code: Int32)
}

final class WhisperEngine {
    private var context: UnsafeMutablePointer<whisper_context>?

    var isLoaded: Bool {
        return context != nil
    }

    func loadModel(at url: URL) throws {
        guard context == nil else { return }
        let path = url.path

        let fileExists = FileManager.default.fileExists(atPath: path)
        let fileSize = (try? FileManager.default.attributesOfItem(atPath: path)[.size] as? NSNumber)?.int64Value
        Logger.info("[WhisperEngine] Loading model at: \(path)")
        Logger.debug("[WhisperEngine] Model exists: \(fileExists), size: \(fileSize.map { String($0) } ?? "unknown")")
        if !fileExists {
            Logger.error("[WhisperEngine] Model file not found at path: \(path)")
            throw WhisperError.modelLoadFailed
        }

        guard FileManager.default.isReadableFile(atPath: path) else {
            Logger.error("[WhisperEngine] Model file exists but is not readable: \(path)")
            throw WhisperError.modelLoadFailed
        }

        if let version = whisper_version() {
            Logger.info("[WhisperEngine] whisper_version: \(String(cString: version))")
        }
        if let systemInfo = whisper_print_system_info() {
            Logger.info("[WhisperEngine] whisper system info: \(String(cString: systemInfo))")
        }

        let ggmlBackendDir = "/opt/homebrew/opt/ggml/libexec"
        let ggmlBackendDirExists = FileManager.default.fileExists(atPath: ggmlBackendDir)
        Logger.debug("[WhisperEngine] ggml backend dir: \(ggmlBackendDir), exists: \(ggmlBackendDirExists)")
        if ggmlBackendDirExists {
            setenv("GGML_BACKEND_PATH", ggmlBackendDir, 1)
            ggml_backend_load_all_from_path(ggmlBackendDir)
            Logger.info("[WhisperEngine] loaded ggml dynamic backends from \(ggmlBackendDir)")
        } else {
            ggml_backend_load_all()
            Logger.info("[WhisperEngine] loaded ggml dynamic backends using default path")
        }

        var contextParams = whisper_context_default_params()
        contextParams.use_gpu = false
        contextParams.flash_attn = false
        contextParams.gpu_device = -1
        contextParams.dtw_token_timestamps = false
        contextParams.dtw_aheads_preset = WHISPER_AHEADS_NONE
        contextParams.dtw_n_top = 0
        contextParams.dtw_aheads = whisper_aheads(n_heads: 0, heads: nil)
        contextParams.dtw_mem_size = 0

        Logger.debug("[WhisperEngine] Whisper params: use_gpu=\(contextParams.use_gpu), flash_attn=\(contextParams.flash_attn), gpu_device=\(contextParams.gpu_device), dtw_n_top=\(contextParams.dtw_n_top), dtw_aheads.n_heads=\(contextParams.dtw_aheads.n_heads)")

        if let paramsRef = whisper_context_default_params_by_ref() {
            let defaultParams = paramsRef.pointee
            Logger.debug("[WhisperEngine] default params by ref: use_gpu=\(defaultParams.use_gpu), flash_attn=\(defaultParams.flash_attn), gpu_device=\(defaultParams.gpu_device)")
        }

        var ctx: UnsafeMutablePointer<whisper_context>? = nil
        path.withCString { cPath in
            ctx = whisper_init_from_file_with_params(cPath, contextParams)
        }

        guard let context = ctx else {
            Logger.error("[WhisperEngine] whisper_init_from_file_with_params returned nil for path: \(path)")
            throw WhisperError.modelLoadFailed
        }
        self.context = context

        self.context = ctx
    }

    func unloadModel() {
        if let ctx = context {
            whisper_free(ctx)
            context = nil
        }
    }

    func transcribe(samples: [Float]) throws -> String {
        guard let ctx = context else { throw WhisperError.libraryUnavailable }
        guard !samples.isEmpty else { return "" }

        Logger.info("[WhisperEngine] Transcribe called; samples count: \(samples.count)")

        // quick RMS check to ensure audio contains signal
        let rms: Float = samples.reduce(0) { $0 + $1 * $1 }
        let rmsVal = sqrt(rms / Float(samples.count))
        Logger.info("[WhisperEngine] audio RMS: \(rmsVal)")

        var params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH)
        params.translate = false
        params.no_timestamps = true
        params.print_progress = false
        params.print_timestamps = false
        // default: allow multiple segments for better phrase splitting
        params.single_segment = false
        // enable automatic language detection so mixed Russian/English works
        params.language = nil
        params.detect_language = true

        // make decoding parameters closer to whisper-cli defaults to avoid false
        // "no speech" detection: increase robustness with small beam/greedy tweaks
        params.n_threads = 4
        params.greedy.best_of = 5
        params.beam_search.beam_size = 5
        // lower the no_speech threshold so short utterances are not discarded
        params.no_speech_thold = 0.3
        // don't aggressively suppress blank/no-speech tokens — can remove valid segments
        params.suppress_blank = false
        params.suppress_nst = false

        Logger.debug("[WhisperEngine] full params: single_segment=\(params.single_segment), detect_language=\(params.detect_language), language=\(params.language == nil ? "nil" : "set"), suppress_blank=\(params.suppress_blank), suppress_nst=\(params.suppress_nst), n_threads=\(params.n_threads), best_of=\(params.greedy.best_of), beam_size=\(params.beam_search.beam_size), no_speech_thold=\(params.no_speech_thold)")

        func runFull(with params: whisper_full_params) -> (Int32, Int) {
            let res = samples.withUnsafeBufferPointer { buffer in
                return whisper_full(ctx, params, buffer.baseAddress, Int32(buffer.count))
            }
            if res != 0 {
                return (res, 0)
            }
            let segs = Int(whisper_full_n_segments(ctx))
            return (res, segs)
        }

        var result: Int32 = -1
        var segmentCount = 0
        // First attempt: default params (autodetect)
        (result, segmentCount) = runFull(with: params)
        // no temporary language string allocated when using autodetect
        Logger.info("[WhisperEngine] whisper_full returned: \(result)")
        Logger.info("[WhisperEngine] whisper_full_n_segments: \(segmentCount)")

        // If no segments were produced, retry with explicit Russian language,
        // then with lower no_speech_thold, then with greedy decoding.
        if result == 0 && segmentCount == 0 {
            Logger.info("[WhisperEngine] No segments — retrying with explicit language=ru")
            var paramsRu = params
            paramsRu.detect_language = false
            var ruPtr: UnsafeMutablePointer<CChar>? = strdup("ru")
            if let lp = ruPtr { paramsRu.language = UnsafePointer(lp) }
            let (res2, segs2) = runFull(with: paramsRu)
            if let lp = ruPtr { free(lp); ruPtr = nil }
            Logger.info("[WhisperEngine] retry(lang=ru) returned: \(res2), segments: \(segs2)")
            if res2 == 0 && segs2 > 0 {
                result = res2; segmentCount = segs2
            }
        }

        if result == 0 && segmentCount == 0 {
            Logger.info("[WhisperEngine] Still no segments — retrying with lower no_speech_thold=0.1")
            var paramsLow = params
            paramsLow.no_speech_thold = 0.1
            let (res3, segs3) = runFull(with: paramsLow)
            Logger.info("[WhisperEngine] retry(no_speech_thold=0.1) returned: \(res3), segments: \(segs3)")
            if res3 == 0 && segs3 > 0 {
                result = res3; segmentCount = segs3
            }
        }

        if result == 0 && segmentCount == 0 {
            Logger.info("[WhisperEngine] Still no segments — retrying with greedy sampling")
            var paramsGreedy = whisper_full_default_params(WHISPER_SAMPLING_GREEDY)
            // copy relevant fields from original params
            paramsGreedy.translate = params.translate
            paramsGreedy.no_timestamps = params.no_timestamps
            paramsGreedy.print_progress = params.print_progress
            paramsGreedy.print_timestamps = params.print_timestamps
            paramsGreedy.single_segment = params.single_segment
            paramsGreedy.detect_language = params.detect_language
            paramsGreedy.language = params.language
            paramsGreedy.suppress_blank = params.suppress_blank
            paramsGreedy.suppress_nst = params.suppress_nst
            paramsGreedy.n_threads = params.n_threads
            paramsGreedy.greedy = params.greedy
            paramsGreedy.beam_search = params.beam_search
            paramsGreedy.no_speech_thold = params.no_speech_thold
            let (res4, segs4) = runFull(with: paramsGreedy)
            Logger.info("[WhisperEngine] retry(greedy) returned: \(res4), segments: \(segs4)")
            if res4 == 0 && segs4 > 0 {
                result = res4; segmentCount = segs4
            }
        }

        guard result == 0 else {
            throw WhisperError.transcriptionFailed(code: result)
        }
        var transcript = ""
        for index in 0..<segmentCount {
            if let segmentPointer = whisper_full_get_segment_text(ctx, Int32(index)) {
                let segment = String(cString: segmentPointer)
                if !transcript.isEmpty {
                    transcript += " "
                }
                transcript += segment
            }
        }

        return transcript.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
