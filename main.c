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
#include <stdatomic.h> 

#include "gui.h"

#include "logging.h"

#define RECOGNITION_IMPLEMENTATION
#include "recognition.h"

#define HOTKEY_IMPLEMENTATION
#include "keyboard.h"

#define AUDIO_IMPLEMENTATION
#include "audio.h"

#define LANG_LENGTH 10

typedef struct {
    float *samples;
    size_t sample_count;
    char lang[LANG_LENGTH];
} WhisperTask;


time_t last_used_timestamp = 0;

char current_lang[LANG_LENGTH] = "auto";

char text_buffer[4096]; // Buffer to hold recognized text

static float audio_buffer[CHUNK_SAMPLES]; 
static WhisperTask background_task;
static atomic_bool is_whisper_busy = false;

extern int gui_run(int pipe_read_fd);
extern void check_mic_permission();
extern void gui_connect_to_window_server();
extern void gui_paste(const char *text);

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


// --- ФОНОВЫЙ ПОТОК РАСПОЗНАВАНИЯ ---
void* async_whisper_worker(void* arg) {
    WhisperTask *task = (WhisperTask*)arg;

    // Вызываем распознавание
    size_t len = recognize_process_buffer(task->samples, task->sample_count, task->lang, text_buffer, sizeof(text_buffer));
    
    if (len > 0) {
        gui_paste(text_buffer);
    }
    
    strncpy(text_buffer, " \0", sizeof(" \0")); // Clear buffer for next use

    // Снимаем блокировку, поток завершает работу
    atomic_store(&is_whisper_busy, false);
    return NULL;
}


void map_lang(char *layout_lang, char *out_lang) {
    // Простейшая маппинг-функция. Можно расширять по мере необходимости.
    if (strcmp(layout_lang, "ru") == 0) {
        strncpy(out_lang, "ru", LANG_LENGTH);
    } else if (strcmp(layout_lang, "pl") == 0) {
        strncpy(out_lang, "en", LANG_LENGTH);
    } else {
        strncpy(out_lang, "auto", LANG_LENGTH); // По умолчанию
    }
}

// --- КОЛЛБЭК ОТ АУДИО ---
void on_audio_chunk_ready(const float *samples, size_t sample_count) {
    // 1. Проверяем, не занят ли еще Whisper (защита от перезаписи буфера)
    bool expected = false;
    if (!atomic_compare_exchange_strong(&is_whisper_busy, &expected, true)) {
        LOG_INFO("Whisper is still processing! Dropping this chunk to avoid memory corruption.");
        return; 
    }

    // 2. Копируем данные в наш статичный буфер
    memcpy(audio_buffer, samples, sample_count * sizeof(float));
    
    // 3. Настраиваем структуру задачи
    background_task.samples = audio_buffer;
    background_task.sample_count = sample_count;

    keyboard_get_layout_language(current_lang, sizeof(current_lang));
    map_lang(current_lang, background_task.lang);

    // 4. Запускаем поток
    pthread_t tid;
    pthread_create(&tid, NULL, async_whisper_worker, &background_task);
    pthread_detach(tid); 
}


// --- КОЛЛБЭК ОТ КЛАВИАТУРЫ (Нажали хоткей) ---
void on_hotkey_pressed() {
    if (!audio_is_recording()) {
        // СТАРТ
        LOG_INFO("Starting record. Lang: %s", current_lang);
        
        recognize_ensure_model_loaded();
        audio_start_recording();
        gui_show("🎙️");
        
        last_used_timestamp = time(NULL);
    } else {
        // СТОП
        LOG_INFO("Stopping record...");
        gui_show("⏳");
        
        // Эта функция внутри audio.c должна остановить микрофон 
        // И СИНХРОННО дергнуть on_audio_chunk_ready() для последнего хвостика аудио
        audio_stop_recording(); 
        
        gui_hide();
    }
}


int run_engine(int pipe_write_fd) {
    gui_set_pipe(pipe_write_fd);
    
    // Инициализируем модули и связываем их через коллбэки
    audio_init(on_audio_chunk_ready);
    keyboard_hotkey_setup(on_hotkey_pressed);
    
    // Запускаем Watchdog
    pthread_t wd_tid;
    pthread_create(&wd_tid, NULL, watchdog_thread, NULL);
    pthread_detach(wd_tid);

    LOG_INFO("Engine running...");
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
    
    return gui_run(pipefd[0]);
}

