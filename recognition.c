#include <stdlib.h>
#include "recognition.h"
#include "whisper.h"
#include "logging.h"
#include "hud.h"

struct whisper_context *ctx = NULL;

void ensure_model_loaded() {
    if (ctx == NULL) {
        ui_send_command("show_wait");
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
        ctx = whisper_init_from_file_with_params("/opt/homebrew/share/whisper-cpp/models/ggml-small.bin", cparams);
        
        if (!ctx) {
            LOG_ERROR("Failed to load Whisper model. File missing or invalid path.");
            exit(1);
        }
        LOG_INFO("Whisper model loaded successfully.");
    }
}

void unload_model() {
    if (ctx != NULL) {
        LOG_INFO("Unloading Whisper model from RAM...");
        whisper_free(ctx);
        ctx = NULL;
    }
}