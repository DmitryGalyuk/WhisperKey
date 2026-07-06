#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mach-o/dyld.h>
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

time_t last_used_timestamp = 0;

AudioState audio_state;

char text_buffer[4096]; // Buffer to hold recognized text

extern int run_hud(int pipe_read_fd);
extern void check_mic_permission();

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
            recognize_unload_model();
        }
    }
    return NULL;
}




void hotkey_handler() {
    // --- TOGGLE LOGIC ---
        if (!audio_state.is_recording) {
            // START RECORDING
            LOG_INFO(">>> TOGGLE: START RECORDING <<<");
            recognize_ensure_model_loaded();
            
            audio_state.is_recording = 1;
            audio_start_recording(&audio_state);
            
            ui_recording();
            last_used_timestamp = time(NULL);
        } else {
            // STOP RECORDING
            LOG_INFO(">>> TOGGLE: STOP RECORDING & PROCESS <<<");
            
            audio_stop_recording(&audio_state);

            ui_waiting();

            text_buffer[0] = '\0'; // Clear the buffer before recognition

            recognize_audio(&audio_state, text_buffer, sizeof(text_buffer));
            
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

    recognize_ensure_model_loaded();
    
    LOG_INFO("Entering CoreFoundation RunLoop...");
    CFRunLoopRun();
    
    return 0;
}

int main(int argc, char **argv) {
    // 1. Child process mode (Engine)
    if (argc == 3 && strcmp(argv[1], "--engine") == 0) {
        int pipe_fd = atoi(argv[2]);
        return run_engine(pipe_fd);
    }

    // 2. Parent process mode (UI / Router)
    printf("[MAIN] Starting WhisperKey Router...\n");
    fflush(stdout);

    check_mic_permission();

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("[MAIN ERROR] Pipe creation failed");
        return 1;
    }

    // Get absolute path for self-execution
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        fprintf(stderr, "[MAIN ERROR] Executable path buffer too small\n");
        return 1;
    }

    printf("[MAIN] Forking process for Engine...\n");
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) {
        perror("[MAIN ERROR] Fork failed");
        return 1;
    }

    if (pid == 0) {
        // CHILD PROCESS
        close(pipefd[0]); // Close read end
        
        char fd_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", pipefd[1]);
        
        // Execute self as engine
        LOG_INFO("Executing engine process: %s --engine %s", path, fd_str);
        execl(path, "WhisperKey", "--engine", fd_str, NULL);
        
        // run_engine(pipefd[1]);

        // // If execl fails:
        perror("[MAIN ERROR] execl failed");
        return 1;
    }

    // PARENT PROCESS
    close(pipefd[1]); // Close write end
    printf("[MAIN] Router successfully spawned Engine (PID: %d). Starting HUD...\n", pid);
    fflush(stdout);
    
    return run_hud(pipefd[0]);
}