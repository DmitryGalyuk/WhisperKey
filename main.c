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

#ifdef DEBUG
#include <execinfo.h>
#endif // DEBUG

#include "gui.h"
#include "logging.h"

#define RECOGNITION_IMPLEMENTATION
#include "recognition.h"

#define HOTKEY_IMPLEMENTATION
#include "keyboard.h"

#define AUDIO_IMPLEMENTATION
#include "audio.h"

#define LANG_LENGTH 10
#define MODEL_UNLOAD_TIMEOUT 600 // 10 minutes

time_t last_used_timestamp = 0;

char current_lang[LANG_LENGTH] = "auto";

char text_buffer[4096]; // Buffer to hold recognized text


extern int gui_run(int pipe_read_fd);
extern void check_mic_permission();
extern void gui_connect_to_window_server();
extern void gui_paste(const char *text);

static pthread_mutex_t whisper_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t whisper_cond = PTHREAD_COND_INITIALIZER;

#define QUEUE_CAPACITY 10 // Хватит на 10 чанков (от 30 до 100 секунд аудио в запасе)

typedef struct {
    float samples[CHUNK_SAMPLES];
    size_t count;
} AudioTask;

typedef struct {
    AudioTask buffer[QUEUE_CAPACITY];
    int head; // Индекс для записи (Producer)
    int tail; // Индекс для чтения (Consumer)
    int count; // Текущее количество чанков в очереди
} AudioQueue;

static AudioQueue audio_queue = { .head = 0, .tail = 0, .count = 0 };

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
        
        // unload model if not used for 10 minutes
        time_t now = time(NULL);
        if (now - last_used_timestamp > MODEL_UNLOAD_TIMEOUT) {
            pthread_mutex_lock(&whisper_mutex);
            recognize_unload_model();
            pthread_mutex_unlock(&whisper_mutex);
            LOG_INFO("Whisper model unloaded due to inactivity.");
        }
    }
    return NULL;
}



void* persistent_whisper_worker(void* arg) {
    (void)arg;
    LOG_INFO("[WHISPER THREAD] Persistent Whisper Worker started.");

    // Память в куче для локальной копии (как мы и делали, чтобы избежать Stack Overflow)
    float *local_samples = (float *)malloc(CHUNK_SAMPLES * sizeof(float));
    if (local_samples == NULL) {
        LOG_ERROR("FATAL: Failed to allocate memory for local_samples");
        return NULL;
    }

    pthread_mutex_lock(&whisper_mutex);

    while (true) {
        // Ждем, пока очередь пуста
        while (audio_queue.count == 0) {
            LOG_DEBUG("[WHISPER THREAD] Queue empty. Sleeping...");
            pthread_cond_wait(&whisper_cond, &whisper_mutex);
            LOG_DEBUG("[WHISPER THREAD] Waking up!");
        }

        // 1. БЕРЕМ ДАННЫЕ ИЗ ХВОСТА ОЧЕРЕДИ
        int tail_idx = audio_queue.tail;
        size_t local_sample_count = audio_queue.buffer[tail_idx].count;
        memcpy(local_samples, audio_queue.buffer[tail_idx].samples, local_sample_count * sizeof(float));
        
        // 2. СДВИГАЕМ ХВОСТ ПО КРУГУ И УМЕНЬШАЕМ СЧЕТЧИК
        audio_queue.tail = (audio_queue.tail + 1) % QUEUE_CAPACITY;
        audio_queue.count--;
        
        LOG_DEBUG("[WHISPER THREAD] Taken chunk from queue. Remaining: %d", audio_queue.count);

        // 3. ОТПУСКАЕМ МЬЮТЕКС!
        pthread_mutex_unlock(&whisper_mutex);

        // 4. ТЯЖЕЛАЯ РАБОТА С НЕЙРОСЕТЬЮ (ВНЕ БЛОКИРОВКИ)
        size_t len = recognize_process_buffer(
            local_samples, 
            local_sample_count, 
            current_lang, 
            text_buffer, 
            sizeof(text_buffer)
        );
        
        if (len > 0) {
            gui_paste(text_buffer);
        }

        text_buffer[0] = '\0'; 

        // 5. ЗАХВАТЫВАЕМ МЬЮТЕКС ДЛЯ СЛЕДУЮЩЕГО ЦИКЛА
        pthread_mutex_lock(&whisper_mutex);
    }
    
    pthread_mutex_unlock(&whisper_mutex);
    free(local_samples);
    return NULL;
}


/**
 * Helper funcction for cases of people using multiple languages and repurpusing the layout. 
 * For example author lives in Poland and uses Polish layout for both English and Polish but still only speaks English.
 * @param layout_lang: layout used for text input
 * @param out_lang: language to be used for Whisper recognition
 */
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

/**
 * Called by microphone module when a chunk of audio is ready for processing.
 * @param samples: array with data from microphone.
 * @param sample_count: Number of samples in the array.
 */
void on_audio_chunk_ready(const float *samples, size_t sample_count) {
    LOG_DEBUG("[MAIN] Audio chunk arrived. Locking the thread");
    
    pthread_mutex_lock(&whisper_mutex);

    // Проверяем, есть ли место в очереди
    if (audio_queue.count < QUEUE_CAPACITY) {
        
        // 1. ПИШЕМ ДАННЫЕ В ГОЛОВУ ОЧЕРЕДИ
        int head_idx = audio_queue.head;
        memcpy(audio_queue.buffer[head_idx].samples, samples, sample_count * sizeof(float));
        audio_queue.buffer[head_idx].count = sample_count;
        
        // 2. СДВИГАЕМ ГОЛОВУ ПО КРУГУ И УВЕЛИЧИВАЕМ СЧЕТЧИК
        audio_queue.head = (audio_queue.head + 1) % QUEUE_CAPACITY;
        audio_queue.count++;
        
        LOG_DEBUG("[MAIN] Added chunk to queue. Total items: %d", audio_queue.count);
        
        // Обновляем язык
        // keyboard_get_layout_language(current_lang, sizeof(current_lang));
        // map_lang(current_lang, current_lang);

        // Будим поток распознавания
        pthread_cond_signal(&whisper_cond);
        
    } else {
        // Очередь переполнена. Защита от Out of Memory.
        LOG_ERROR("[MAIN] WARNING: Audio queue is FULL! Dropping chunk.");
    }

    pthread_mutex_unlock(&whisper_mutex);
    LOG_DEBUG("[MAIN] Recognition thread unlocked");
}


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


/**
 * Runs the Whisper engine in a separate process.
 * @param pipe_write_fd: File descriptor for writing to the parent UI process.
 */
int run_engine(int pipe_write_fd) {
    gui_set_pipe(pipe_write_fd);
    
    // init audio and hotkeys
    audio_init(on_audio_chunk_ready);
    keyboard_hotkey_setup(on_hotkey_pressed);
    
    // start Watchdog thread
    pthread_t wd_tid;
    pthread_create(&wd_tid, NULL, watchdog_thread, NULL);
    pthread_detach(wd_tid);
    LOG_INFO("Watchdog thread started.");

    // start Whisper worker thread
    pthread_t recognition_thread;
    pthread_create(&recognition_thread, NULL, persistent_whisper_worker, NULL);
    pthread_detach(recognition_thread);
    LOG_INFO("Whisper worker thread started.");

    last_used_timestamp = time(NULL);

    LOG_INFO("Engine running...");
    CFRunLoopRun();
    return 0;
}

#ifdef DEBUG
void crash_handler(int sig) {
    void *array[20];
    size_t size;

    size = backtrace(array, 20);
    
    LOG_ERROR("=====================================");
    LOG_ERROR("FATAL CRASH! Engine died with signal %d", sig);
    LOG_ERROR("=====================================");
    
    // Печатаем стек прямо в stderr
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    
    exit(1);
}
#endif // DEBUG

int main(int argc, char **argv) {

    #ifdef DEBUG
    signal(SIGSEGV, crash_handler); // Ошибка работы с памятью (Segfault)
    signal(SIGABRT, crash_handler); // Abort (вызывается системой или assert)
    signal(SIGILL, crash_handler);  // Недопустимая инструкция (например, кривая линковка Metal)
    signal(SIGBUS, crash_handler);
    #endif // DEBUG

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
    
        perror("[MAIN ERROR] execl failed");
        return 1;
    }

    // PARENT PROCESS
    close(pipefd[1]); // Close write end
    printf("[MAIN] Router successfully spawned Engine (PID: %d). Starting HUD...\n", pid);
    fflush(stdout);
    
    return gui_run(pipefd[0]);
}

