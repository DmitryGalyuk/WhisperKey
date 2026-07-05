void paste_text(const char *text);

#ifdef PASTE_IMPLEMENTATION

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

#endif // PASTE_IMPLEMENTATION