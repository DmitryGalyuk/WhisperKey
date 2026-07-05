#include "hotkey.h"
#include "logging.h"
#include <ApplicationServices/ApplicationServices.h>

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
void hotkey_setup( void(*callback)() ) {
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