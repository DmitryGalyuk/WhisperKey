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

// --- Helper Functions ---

void start_recording() {
    recording_pid = fork();
    if (recording_pid == 0) {
        char *args[] = {"ffmpeg", "-f", "avfoundation", "-i", ":1", "-y",
                        "./voice.wav", "-nostats", "-nostdin", "-loglevel", "0", NULL};
        execvp(args[0], args);
        exit(1);
    } else if (recording_pid > 0) {
        printf("[START] Recording started. PID: %d\n", recording_pid);
        is_recording = true;
    }
}

void paste_clipboard() {
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
    printf("[CLIPBOARD] Text pasted.\n");
}

void process_audio_and_paste(pid_t pid_to_kill) {
    printf("[STOP] Stopping ffmpeg (PID: %d)...\n", pid_to_kill);
    kill(pid_to_kill, SIGINT);
    waitpid(pid_to_kill, NULL, 0);

    printf("File saved! Running Whisper...\n");
    system("whisper-cli -m /opt/homebrew/share/whisper-cpp/models/ggml-small.bin -l ru -f ./voice.wav -otxt -mc 0 -et 2.8 > /dev/null 2>&1");

    FILE *txt_file = fopen("./voice.wav.txt", "r");
    if (txt_file) {
        FILE *pbcopy = popen("pbcopy", "w");
        if (pbcopy) {
            char buffer[1024];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), txt_file)) > 0) {
                fwrite(buffer, 1, bytes_read, pbcopy);
            }
            pclose(pbcopy);
        }
        fclose(txt_file);
        printf("[CLIPBOARD] Text copied.\n");
        paste_clipboard();
    }
}

void* worker_thread_func(void* arg) {
    process_audio_and_paste((pid_t)(uintptr_t)arg);
    return NULL;
}

// --- Event Handler ---

CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon) {
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(proxy, true);
        return event;
    }

    CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

    if (keycode == TARGET_KEYCODE && type == kCGEventKeyDown) {
        if (CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat)) return NULL;

        if (!is_recording) {
            start_recording();
        } else {
            is_recording = false;
            pid_t pid_to_kill = recording_pid;
            recording_pid = -1;

            pthread_t worker;
            pthread_create(&worker, NULL, worker_thread_func, (void*)(uintptr_t)pid_to_kill);
            pthread_detach(worker);
        }
        return NULL;
    }
    return event;
}

// --- Main Loop ---

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

    printf("Daemon running. Waiting for trigger key...\n");
    CFRunLoopRun();
    return 0;
}