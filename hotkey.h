#include "logging.h"
#include <ApplicationServices/ApplicationServices.h>

#ifndef HOTKEY_INCLUDED
#define HOTKEY_INCLUDED

void keyboard_hotkey_setup( void(*callback)() );
void keyboard_paste(const char *text);

#endif // HOTKEY_INCLUDED

#ifdef HOTKEY_IMPLEMENTATION

#define HOTKEY_KEYCODE 176 // Mic keycode

CFMachPortRef eventTap;
void(*event_handler)();

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

        event_handler(); // Call the provided callback function
        return NULL;
    }
    
    return event;
}

// В функции run_engine вместо Carbon:
void keyboard_hotkey_setup( void(*callback)() ) {
    event_handler = callback;

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

void keyboard_paste(const char *text) {
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


void get_system_language(char *lang_buf, size_t buf_size) {
    // Дефолтное значение на случай, если что-то пойдет не так
    strncpy(lang_buf, "en", buf_size);

    CFLocaleRef current_locale = CFLocaleCopyCurrent();
    if (current_locale) {
        CFStringRef lang_code = CFLocaleGetValue(current_locale, kCFLocaleLanguageCode);
        if (lang_code) {
            CFStringGetCString(lang_code, lang_buf, buf_size, kCFStringEncodingUTF8);
        }
        CFRelease(current_locale);
    }
}

#endif // HOTKEY_IMPLEMENTATION