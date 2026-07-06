#include <stdlib.h>

#include "whisper.h"
#include "audio.h"
#include "logging.h"
#include "gui.h"


#ifndef RECOGNITION_INCLUDED
#define RECOGNITION_INCLUDED

void recognize_ensure_model_loaded();
void recognize_unload_model();
size_t recognize_audio(AudioState *audio_state, char *text_buffer, size_t buffer_size);

#endif // RECOGNITION_INCLUDED


#ifdef RECOGNITION_IMPLEMENTATION

struct whisper_context *ctx = NULL;

void recognize_ensure_model_loaded() {
    if (ctx == NULL) {
        gui_waiting();
        LOG_INFO("Loading Whisper model into RAM...");
        
        struct whisper_context_params cparams = whisper_context_default_params();

        cparams.use_gpu = false;
        cparams.flash_attn = false;
        cparams.gpu_device = -1;
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
        ctx = whisper_init_from_file_with_params("/opt/homebrew/share/whisper-cpp/models/ggml-base.bin", cparams);
        
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
size_t recognize_audio(AudioState *audio_state, char *text_buffer, size_t buffer_size) {
    struct whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.language = "ru"; // Or "ru" to force Russian
    wparams.n_threads = 4;
    wparams.print_progress = false;
    wparams.print_timestamps = false;
    wparams.no_context = true;
    
    // Run neural network
    if (whisper_full(ctx, wparams, audio_state->samples, audio_state->sample_count) != 0) {
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
        
        strncat(text_buffer, text, buffer_size - strlen(text_buffer) - 1);
        strncat(text_buffer, " ", buffer_size - strlen(text_buffer) - 1);
    }

    // Remove trailing space
    if (strlen(text_buffer) > 0) {
        text_buffer[strlen(text_buffer) - 1] = '\0';
    }

    return strlen(text_buffer);
}

#endif // RECOGNITION_IMPLEMENTATION