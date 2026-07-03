#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/param.h> // Для MAXPATHLEN

#define TARGET_KEYCODE 176 // Microphone button keycode

bool is_recording = false;
pid_t recording_pid = -1;

// Глобальные пути, которые мы сгенерируем при старте
char wav_path[MAXPATHLEN];
char txt_path[MAXPATHLEN];
const char *model_path = NULL;

// ==========================================
// 1. AUDIO INDICATORS
// ==========================================

void play_sound_start() {
    system("afplay /System/Library/Sounds/Tink.aiff &");
}

void play_sound_processing() {
    system("afplay /System/Library/Sounds/Pop.aiff &");
}

void play_sound_done() {
    system("afplay /System/Library/Sounds/Glass.aiff &");
}

// ==========================================
// 2. AUDIO RECORDING (FFMPEG)
// ==========================================

void start_audio_recording() {
    recording_pid = fork();
    if (recording_pid == 0) {
        // Используем сгенерированный путь wav_path
        char *args[] = {"ffmpeg", "-f", "avfoundation", "-i", ":1", "-y",
                        wav_path, "-nostats", "-nostdin", "-loglevel", "0", NULL};
        execvp(args[0], args);
        exit(1);
    } else if (recording_pid > 0) {
        printf("[AUDIO] Recording started. PID: %d\n", recording_pid);
    }
}

void stop_audio_recording(pid_t pid) {
    if (pid > 0) {
        printf("[AUDIO] Stopping ffmpeg (PID: %d)...\n", pid);
        kill(pid, SIGINT);
        waitpid(pid, NULL, 0);
    }
}

// ==========================================
// 3. TRANSCRIPTION (WHISPER)
// ==========================================

void run_whisper_transcription() {
    printf("[AI] Running Whisper with model: %s\n", model_path);
    
    char cmd[2048];
    // Собираем команду динамически, подставляя путь к модели и временному файлу
    snprintf(cmd, sizeof(cmd), 
             "whisper-cli -m '%s' -l ru -f '%s' -otxt -mc 0 -et 2.8 > /dev/null 2>&1", 
             model_path, wav_path);
    
    system(cmd);
}

// ==========================================
// 4. CLIPBOARD & CLEANUP
// ==========================================

bool copy_file_to_clipboard(const char* filepath) {
    FILE *txt_file = fopen(filepath, "r");
    if (!txt_file) {
        printf("[ERROR] Transcription file not found.\n");
        return false;
    }

    FILE *pbcopy = popen("pbcopy", "w");
    if (!pbcopy) {
        fclose(txt_file);
        return false;
    }

    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), txt_file)) > 0) {
        fwrite(buffer, 1, bytes_read, pbcopy);
    }
    
    pclose(pbcopy);
    fclose(txt_file);
    printf("[CLIPBOARD] Text copied.\n");
    return true;
}

void emulate_keyboard_paste() {
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef v_down = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, true);
    CGEventRef v_up = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, false);

    CGEventSetFlags(v_down, kCGEventFlagMaskCommand);
    CGEventSetFlags(v_up, kCGEventFlagMaskCommand);

    CGEventPost(kCGHIDEventTap, v_down);
    CGEventPost(kCGHIDEventTap, v_up);

    CFRelease(v_down);
    CFRelease(v_up);
    CFRelease(source);
    printf("[CLIPBOARD] Text pasted via Cmd+V.\n");
}

void cleanup_temp_files() {
    // Системный вызов unlink удаляет файлы с диска
    unlink(wav_path);
    unlink(txt_path);
    printf("[CLEANUP] Temporary files removed.\n");
}

// ==========================================
// 5. WORKER THREAD (ORCHESTRATOR)
// ==========================================

void* process_pipeline_thread(void* arg) {
    pid_t pid_to_kill = (pid_t)(uintptr_t)arg;

    stop_audio_recording(pid_to_kill);
    run_whisper_transcription();

    if (copy_file_to_clipboard(txt_path)) {
        emulate_keyboard_paste();
        play_sound_done(); 
    }
    
    cleanup_temp_files(); // Убираем за собой в любом случае
    return NULL;
}

// ==========================================
// 6. EVENT HANDLER
// ==========================================

CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon) {
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(proxy, true);
        return event;
    }

    CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

    if (keycode == TARGET_KEYCODE && type == kCGEventKeyDown) {
        if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat)) return NULL;

        if (!is_recording) {
            is_recording = true;
            play_sound_start();      
            start_audio_recording(); 
        } else {
            is_recording = false;
            pid_t pid_to_kill = recording_pid;
            recording_pid = -1;

            play_sound_processing(); 

            pthread_t worker;
            pthread_create(&worker, NULL, process_pipeline_thread, (void*)(uintptr_t)pid_to_kill);
            pthread_detach(worker);
        }
        return NULL; 
    }
    return event;
}

// ==========================================
// 7. MAIN
// ==========================================

int main(int argc, char *argv[]) {
    // Проверяем аргументы командной строки
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_whisper_model.bin>\n", argv[0]);
        fprintf(stderr, "Example: %s /opt/homebrew/share/whisper-cpp/models/ggml-small.bin\n", argv[0]);
        return 1;
    }
    model_path = argv[1];

    // Инициализируем пути для временных файлов
    const char *tmp_dir = getenv("TMPDIR");
    if (!tmp_dir) tmp_dir = "/tmp/"; // Fallback, если TMPDIR не задан

    // Генерируем уникальные имена на основе PID нашего демона
    snprintf(wav_path, sizeof(wav_path), "%swhisperkey_%d.wav", tmp_dir, getpid());
    snprintf(txt_path, sizeof(txt_path), "%swhisperkey_%d.wav.txt", tmp_dir, getpid());

    printf("[INIT] Model path: %s\n", model_path);
    printf("[INIT] Temp audio: %s\n", wav_path);

    CGEventMask eventMask = (1 << kCGEventKeyDown);
    CFMachPortRef eventTap = CGEventTapCreate(
        kCGSessionEventTap, kCGHeadInsertEventTap, 0, eventMask, eventCallback, NULL);

    if (!eventTap) {
        fprintf(stderr, "ERROR: Accessibility permissions required.\n");
        return 1;
    }

    CFRunLoopSourceRef runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes);
    CGEventTapEnable(eventTap, true);

    printf("Daemon running. Waiting for trigger key...\n");
    CFRunLoopRun();
    return 0;
}