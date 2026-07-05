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

#define HOTKEY_KEYCODE 176 // Mic keycode

// extern void ui_send_command(const char *cmd);

// --- Global State ---

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



// Callback, который будет вызываться при каждом нажатии клавиш
CGEventRef hotkey_callback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *userInfo) {
    (void)proxy;
    (void)userInfo;

    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(eventTap, true);
        return event;
    }

    CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

    // Filter only for our key (176) and only for KeyDown (to avoid double triggers on repeat)
    if (keycode == HOTKEY_KEYCODE && type == kCGEventKeyDown) {
        if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat)) return NULL;

        // --- TOGGLE LOGIC ---
        pthread_mutex_lock(&audio_mutex);
        if (!is_recording) {
            // START RECORDING
            LOG_INFO(">>> TOGGLE: START RECORDING <<<");
            ensure_model_loaded();
            
            is_recording = 1;
            pthread_cond_signal(&audio_cond); // Signal audio thread to wake up
            
            ui_send_command("show_mic");
            last_used_timestamp = time(NULL);
        } else {
            // STOP RECORDING
            LOG_INFO(">>> TOGGLE: STOP RECORDING & PROCESS <<<");
            
            is_recording = 0;
            pthread_cond_signal(&audio_cond); // Wake up to notice recording is 0
            
            ui_send_command("show_wait");
            
            // Here you will eventually trigger whisper_full()
            paste_text("Test Recognition String (Toggle Mode)");
            
            ui_send_command("hide");
        }
        pthread_mutex_unlock(&audio_mutex);
        
        return NULL; // Consume the event
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
    ui_set_pipe(pipe_write_fd);
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

