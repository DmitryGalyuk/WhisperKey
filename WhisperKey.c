#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <pthread.h>

#define TARGET_KEYCODE 176 // Microphone button keycode

bool is_recording = false;
pid_t recording_pid = -1;

// ==========================================
// 1. АУДИО ИНДИКАТОРЫ (ЗВУКИ СИСТЕМЫ)
// ==========================================

void play_sound_start() {
    // Амперсанд в конце обязателен, чтобы звук играл в фоне и не тормозил хук
    system("afplay /System/Library/Sounds/Tink.aiff &");
}

void play_sound_processing() {
    system("afplay /System/Library/Sounds/Pop.aiff &");
}

void play_sound_done() {
    system("afplay /System/Library/Sounds/Glass.aiff &");
}

// ==========================================
// 2. РАБОТА СО ЗВУКОМ (FFMPEG)
// ==========================================

void start_audio_recording() {
    recording_pid = fork();
    if (recording_pid == 0) {
        char *args[] = {"ffmpeg", "-f", "avfoundation", "-i", ":1", "-y",
                        "./voice.wav", "-nostats", "-nostdin", "-loglevel", "0", NULL};
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
// 3. РАСПОЗНАВАНИЕ (WHISPER)
// ==========================================

void run_whisper_transcription() {
    printf("[AI] Running Whisper...\n");
    system("whisper-cli -m /opt/homebrew/share/whisper-cpp/models/ggml-small.bin -l ru -f ./voice.wav -otxt -mc 0 -et 2.8 > /dev/null 2>&1");
}

// ==========================================
// 4. БУФЕР ОБМЕНА И ВВОД
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

// ==========================================
// 5. ОРКЕСТРАТОР (РАБОЧИЙ ПОТОК)
// ==========================================

void* process_pipeline_thread(void* arg) {
    pid_t pid_to_kill = (pid_t)(uintptr_t)arg;

    stop_audio_recording(pid_to_kill);
    run_whisper_transcription();

    if (copy_file_to_clipboard("./voice.wav.txt")) {
        emulate_keyboard_paste();
        play_sound_done(); // Сигнализируем, что всё готово и вставлено
    }
    
    return NULL;
}

// ==========================================
// 6. СИСТЕМНЫЙ ХУК (ОБРАБОТЧИК КЛАВИАТУРЫ)
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
            play_sound_start();      // 1. Озвучиваем старт
            start_audio_recording(); // 2. Начинаем писать звук
        } else {
            is_recording = false;
            pid_t pid_to_kill = recording_pid;
            recording_pid = -1;

            play_sound_processing(); // Озвучиваем уход в обработку

            // Отправляем всю тяжелую работу в отдельный поток
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

int main() {
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

    printf("Daemon running with audio feedback. Waiting for trigger key...\n");
    CFRunLoopRun();
    return 0;
}