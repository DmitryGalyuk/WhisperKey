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
#include "recognition.h"
#include "hud.h"

#define HOTKEY_IMPLEMENTATION
#include "hotkey.c"

#define PASTE_IMPLEMENTATION
#include "paste.c"

// --- Global State ---

time_t last_used_timestamp = 0;

pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t audio_cond = PTHREAD_COND_INITIALIZER;
int is_recording = 0;



// --- Utilities ---




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

void *audio_thread(void *arg) {
    (void)arg;
    LOG_DEBUG("Audio capture thread started (Sleeping).");
    
    while (1) {
        pthread_mutex_lock(&audio_mutex);
        while (!is_recording) {
            pthread_cond_wait(&audio_cond, &audio_mutex);
        }
        pthread_mutex_unlock(&audio_mutex);
        
        // Dummy loop simulating audio capture
        usleep(100000); 
    }
    return NULL;
}

void hotkey_handler() {
    // --- TOGGLE LOGIC ---
        pthread_mutex_lock(&audio_mutex);
        if (!is_recording) {
            // START RECORDING
            LOG_INFO(">>> TOGGLE: START RECORDING <<<");
            ensure_model_loaded();
            
            is_recording = 1;
            pthread_cond_signal(&audio_cond); // Signal audio thread to wake up
            
            ui_recording();
            last_used_timestamp = time(NULL);
        } else {
            // STOP RECORDING
            LOG_INFO(">>> TOGGLE: STOP RECORDING & PROCESS <<<");
            
            is_recording = 0;
            pthread_cond_signal(&audio_cond); // Wake up to notice recording is 0
            
            ui_waiting();
            
            // Here you will eventually trigger whisper_full()
            paste_text("Test Recognition String (Toggle Mode)");
            
            ui_hide();
        }
        pthread_mutex_unlock(&audio_mutex);
}


int run_engine(int pipe_write_fd) {
    ui_set_pipe(pipe_write_fd);
    last_used_timestamp = time(NULL);
    
    LOG_INFO("Engine process started. PID: %d", getpid());
    
    // Check and prompt for permissions FIRST
    // request_accessibility_permissions();
    
    pthread_t wd_tid, audio_tid;
    pthread_create(&wd_tid, NULL, watchdog_thread, NULL);
    pthread_create(&audio_tid, NULL, audio_thread, NULL);
    
    hotkey_setup(&hotkey_handler);
    
    LOG_INFO("Entering CoreFoundation RunLoop...");
    CFRunLoopRun();
    
    return 0;
}

