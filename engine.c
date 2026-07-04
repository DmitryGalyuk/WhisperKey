#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include "whisper.h"
#include <signal.h>
#include "logging.h"


#define HOTKEY_KEYCODE 176 // Mic keycode

// --- Global State ---
int ipc_fd = -1;
struct whisper_context *ctx = NULL;
time_t last_used_timestamp = 0;

pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t audio_cond = PTHREAD_COND_INITIALIZER;
int is_recording = 0;

CFMachPortRef eventTap;



// --- Security / Permissions ---
void request_accessibility_permissions() {
    LOG_INFO("Checking macOS Accessibility permissions...");
    
    const void *keys[] = { kAXTrustedCheckOptionPrompt };
    const void *values[] = { kCFBooleanTrue };
    
    CFDictionaryRef options = CFDictionaryCreate(NULL, keys, values, 1, 
                                                 &kCFCopyStringDictionaryKeyCallBacks, 
                                                 &kCFTypeDictionaryValueCallBacks);
    
    bool is_trusted = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    
    if (!is_trusted) {
        LOG_ERROR("Accessibility permissions missing! A system prompt should appear.");
        LOG_ERROR("Please grant permissions to your Terminal in System Settings -> Privacy & Security -> Accessibility.");
        LOG_ERROR("Then restart the application.");
    } else {
        LOG_INFO("Accessibility permissions are granted. Global hotkeys will work.");
    }
}

// --- Utilities ---
void send_ui_command(const char *cmd) {
    if (ipc_fd != -1) {
        write(ipc_fd, cmd, strlen(cmd));
        write(ipc_fd, "\n", 1);
        LOG_DEBUG("Sent IPC command: %s", cmd);
    }
}

void paste_text(const char *text) {
    if (!text || strlen(text) == 0) return;
    
    LOG_INFO("Pasting text to active window: %s", text);
    
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "printf '%%s' '%s' | pbcopy", text);
    system(cmd);

    // Emulate Cmd+V
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef vDown = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, true);
    CGEventSetFlags(vDown, kCGEventFlagMaskCommand);
    CGEventRef vUp = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, false);
    
    CGEventPost(kCGHIDEventTap, vDown);
    CGEventPost(kCGHIDEventTap, vUp);
    
    CFRelease(vDown);
    CFRelease(vUp);
    CFRelease(source);
}

// --- Whisper Core ---
void ensure_model_loaded() {
    if (ctx == NULL) {
        send_ui_command("show_wait");
        LOG_INFO("Loading Whisper model into RAM...");
        
        struct whisper_context_params cparams = whisper_context_default_params();
        
        // IMPORTANT: Ensure this path is correct for your system!
        ctx = whisper_init_from_file_with_params("/opt/homebrew/share/whisper-cpp/models/ggml-small.bin", cparams);
        
        if (!ctx) {
            LOG_ERROR("Failed to load Whisper model. File missing or invalid path.");
            exit(1);
        }
        LOG_INFO("Whisper model loaded successfully.");
    }
}

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
        if (ctx != NULL && (now - last_used_timestamp > 600)) {
            LOG_INFO("Idle for 10 minutes. Unloading model...");
            whisper_free(ctx);
            ctx = NULL;
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



// Callback, который будет вызываться при каждом нажатии клавиш
CGEventRef hotkey_callback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *userInfo) {
if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(eventTap, true);
        return event;
    }

    CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

    if (keycode == HOTKEY_KEYCODE && type == kCGEventKeyDown) {
        if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat)) return NULL;

            LOG_INFO(">>> START RECORDING <<<");
            ensure_model_loaded();
            
            pthread_mutex_lock(&audio_mutex);
            is_recording = 1; 
            pthread_cond_signal(&audio_cond);
            pthread_mutex_unlock(&audio_mutex);
            
            send_ui_command("show_mic");
            last_used_timestamp = time(NULL);
    } else {
            LOG_INFO(">>> STOP RECORDING <<<");
            pthread_mutex_lock(&audio_mutex);
            is_recording = 0;
            pthread_mutex_unlock(&audio_mutex);
            
            send_ui_command("show_wait");
            paste_text("Test Recognition String");
            send_ui_command("hide");
    }
    
    return event;
}

// В функции run_engine вместо Carbon:
void setup_hotkey_tap() {
    CGEventMask eventMask = (1 << kCGEventKeyDown);
    eventTap = CGEventTapCreate(
        kCGSessionEventTap, kCGHeadInsertEventTap, 0, eventMask, hotkey_callback, NULL);

    if (!eventTap) {
        fprintf(stderr, "ERROR: Accessibility permissions required.\n");
        request_accessibility_permissions();
    }

    CFRunLoopSourceRef runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes);
    CGEventTapEnable(eventTap, true);
}

int run_engine(int pipe_write_fd) {
    ipc_fd = pipe_write_fd;
    last_used_timestamp = time(NULL);
    
    LOG_INFO("Engine process started. PID: %d", getpid());
    
    // Check and prompt for permissions FIRST
    // request_accessibility_permissions();
    
    pthread_t wd_tid, audio_tid;
    pthread_create(&wd_tid, NULL, watchdog_thread, NULL);
    pthread_create(&audio_tid, NULL, audio_thread, NULL);
    
    setup_hotkey_tap();
    
    LOG_INFO("Entering CoreFoundation RunLoop...");
    CFRunLoopRun();
    
    return 0;
}

