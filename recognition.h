#include <stdlib.h>

#include "whisper.h"
#include "audio.h"
#include "logging.h"
#include "gui.h"


#ifndef RECOGNITION_INCLUDED
#define RECOGNITION_INCLUDED

void recognize_ensure_model_loaded();
void recognize_unload_model();
size_t recognize_process_buffer(const float *samples, size_t sample_count, const char *lang, char *out_text, size_t max_len);

#endif // RECOGNITION_INCLUDED


#ifdef RECOGNITION_IMPLEMENTATION

struct whisper_context *ctx = NULL;

void recognize_ensure_model_loaded() {
    if (ctx == NULL) {
        gui_show("⏳");
        LOG_INFO("Loading Whisper model into RAM...");
        
        struct whisper_context_params cparams = whisper_context_default_params();

        // cparams.use_gpu = false;
        cparams.flash_attn = false;
        // cparams.gpu_device = -1;
        cparams.dtw_token_timestamps = false;
        cparams.dtw_aheads_preset = WHISPER_AHEADS_NONE;
        cparams.dtw_n_top = 0;
        cparams.dtw_aheads = (whisper_aheads){.n_heads = 0, .heads = NULL};
        cparams.dtw_mem_size = 0;

        char* ggmlBackendDir = "/opt/homebrew/opt/ggml/libexec";
        setenv("GGML_BACKEND_PATH", ggmlBackendDir, 1);
        ggml_backend_load_all_from_path(ggmlBackendDir);
        LOG_INFO("[WhisperEngine] loaded ggml dynamic backends from %s", ggmlBackendDir);
        
        // IMPORTANT: Ensure this path is correct for your system!
        // ctx = whisper_init_from_file_with_params("/opt/homebrew/share/whisper-cpp/models/ggml-small.bin", cparams);
        // ctx = whisper_init_from_file_with_params("/opt/homebrew/share/whisper-cpp/models/ggml-base.bin", cparams);
        ctx = whisper_init_from_file_with_params("/opt/homebrew/share/whisper-cpp/models/ggml-medium.bin", cparams);
        
        if (!ctx) {
            LOG_ERROR("Failed to load Whisper model. File missing or invalid path.");
            exit(1);
        }
        LOG_INFO("Whisper model loaded successfully.");
    }
}

void recognize_unload_model() {
    if (ctx != NULL) {
        LOG_INFO("Unloading Whisper model from RAM...");
        whisper_free(ctx);
        ctx = NULL;
    }
}

// returns the number of characters written to text_buffer (excluding null terminator)
// -1 in case of error 
/** Recognizes speech from the provided audio samples using the Whisper model.
 * @param samples: Array of audio samples
 * @param sample_count: Number of samples in the array
 * @param lang: Language code for recognition (e.g., "en", "ru", "auto")
 * @param out_text: Buffer to store the recognized text
 * @param max_len: Maximum length of the output buffer
 * @returns Number of characters written to out_text (excluding null terminator), or -1 on error
 * @retval Number of characters written to out_text (excluding null terminator)
 * @retval -1 if recognition fails or no speech is detected
 */
size_t recognize_process_buffer(const float *samples, size_t sample_count, 
                                const char *lang, char *out_text, size_t max_len) {
    struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.language = lang; // Or "ru" to force Russian
    wparams.n_threads = 4;
    wparams.print_progress = false;
    wparams.print_timestamps = false;
    wparams.no_context = true;
    
    // Run neural network
    if (whisper_full(ctx, wparams, samples, sample_count) != 0) {
        LOG_ERROR("Failed to process audio with Whisper");
        return -1;
    }

    int n_segments = whisper_full_n_segments(ctx);
    if (n_segments == 0) {
        LOG_INFO("No speech recognized.");
        return -1;
    }

    // Allocate buffer for final text
    for (int i = 0; i < n_segments; ++i) {
        const char *text = whisper_full_get_segment_text(ctx, i);
        
        // Skip Whisper hallucinations (often enclosed in brackets)
        if (text[0] == '[' || text[0] == '(') continue; 
        
        // Trim leading spaces safely
        while (*text == ' ') text++;
        
        strncat(out_text, text, max_len - strlen(out_text) - 1);
        strncat(out_text, " ", max_len - strlen(out_text) - 1);
    }

    // Remove trailing space
    if (strlen(out_text) > 0) {
        out_text[strlen(out_text) - 1] = '\0';
    }

    return strlen(out_text);
}

#endif // RECOGNITION_IMPLEMENTATION