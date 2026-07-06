#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <signal.h>

#include "logging.h"
#include "hud.h"

#define RECOGNITION_IMPLEMENTATION
#include "recognition.c"

#define HOTKEY_IMPLEMENTATION
#include "hotkey.c"

#define PASTE_IMPLEMENTATION
#include "paste.c"

#define AUDIO_IMPLEMENTATION
#include "audio.c"

// --- Global State ---

time_t last_used_timestamp = 0;

AudioState audio_state;

char text_buffer[4096]; // Buffer to hold recognized text

// --- Threads ---
void *watchdog_thread(void *arg) {
    (void)arg;
    
    // Получаем PID родителя
    pid_t parent_pid = getppid();
    
    while (1) {
        sleep(5); // Проверяем каждые 5 секунд
        
        // kill(pid, 0) не убивает процесс, а просто проверяет, жив ли он
        if (kill(parent_pid, 0) == -1) {
            LOG_ERROR("Parent process died. Engine exiting...");
            exit(0); // Родитель умер, выходим вместе с ним
        }
        
        // Старая логика проверки таймаута модели
        time_t now = time(NULL);
        if (now - last_used_timestamp > 600) {
            unload_model();
        }
    }
    return NULL;
}




void hotkey_handler() {
    // --- TOGGLE LOGIC ---
        if (!audio_state.is_recording) {
            // START RECORDING
            LOG_INFO(">>> TOGGLE: START RECORDING <<<");
            ensure_model_loaded();
            
            audio_state.is_recording = 1;
            
            ui_recording();
            last_used_timestamp = time(NULL);
        } else {
            // STOP RECORDING
            LOG_INFO(">>> TOGGLE: STOP RECORDING & PROCESS <<<");
            
            audio_state.is_recording = 0;
            
            ui_waiting();

            recognize_audio(audio_state, text_buffer, sizeof(text_buffer));
            
            // Here you will eventually trigger whisper_full()
            paste_text(text_buffer);
            
            ui_hide();
        }
}


int run_engine(int pipe_write_fd) {
    ui_set_pipe(pipe_write_fd);
    last_used_timestamp = time(NULL);
    
    LOG_INFO("Engine process started. PID: %d", getpid());
    
    // Check and prompt for permissions FIRST
    // request_accessibility_permissions();
    
    
    hotkey_setup(&hotkey_handler);
    
    LOG_INFO("Entering CoreFoundation RunLoop...");
    CFRunLoopRun();
    
    return 0;
}

