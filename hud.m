#import <Cocoa/Cocoa.h>
#include <unistd.h>

NSWindow *overlayWindow;
NSTextField *emojiLabel;

void setup_window() {
    NSLog(@"[HUD] Initializing transparent overlay window...");
    
    NSRect frame = NSMakeRect(0, 0, 100, 100);
    overlayWindow = [[NSWindow alloc] initWithContentRect:frame
                                                styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    
    [overlayWindow setBackgroundColor:[NSColor clearColor]];
    [overlayWindow setOpaque:NO];
    [overlayWindow setLevel:NSScreenSaverWindowLevel];
    [overlayWindow setIgnoresMouseEvents:YES];
    
    emojiLabel = [[NSTextField alloc] initWithFrame:frame];
    [emojiLabel setEditable:NO];
    [emojiLabel setBezeled:NO];
    [emojiLabel setDrawsBackground:NO];
    [emojiLabel setFont:[NSFont systemFontOfSize:64]];
    [emojiLabel setAlignment:NSTextAlignmentCenter];
    
    [[overlayWindow contentView] addSubview:emojiLabel];
    NSLog(@"[HUD] Window initialized successfully.");
}

void move_window_to_cursor() {
    NSPoint mouseLoc = [NSEvent mouseLocation];
    NSPoint windowLoc = NSMakePoint(mouseLoc.x + 20, mouseLoc.y - 80);
    [overlayWindow setFrameOrigin:windowLoc];
}

void start_pipe_listener(int fd) {
    NSLog(@"[HUD] Starting asynchronous pipe listener on FD: %d", fd);
    
    dispatch_queue_t queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
    dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, fd, 0, queue);
    
    dispatch_source_set_event_handler(source, ^{
        char buffer[256];
        ssize_t bytesRead = read(fd, buffer, sizeof(buffer) - 1);
        
        if (bytesRead <= 0) {
            NSLog(@"[HUD] Pipe closed or error. Engine died. Exiting HUD.");
            exit(0);
        }
        
        buffer[bytesRead] = '\0';
        NSString *command = [NSString stringWithUTF8String:buffer];
        NSLog(@"[HUD] Received command from Engine: %@", [command stringByTrimmingCharactersInSet:[NSCharacterSet newlineCharacterSet]]);
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if ([command containsString:@"show_mic"]) {
                [emojiLabel setStringValue:@"🎙️"];
                move_window_to_cursor();
                [overlayWindow orderFront:nil];
            } 
            else if ([command containsString:@"show_wait"]) {
                [emojiLabel setStringValue:@"⏳"];
                [overlayWindow orderFront:nil];
            } 
            else if ([command containsString:@"hide"]) {
                [overlayWindow orderOut:nil];
            }
        });
    });
    
    dispatch_resume(source);
}

int run_hud(int pipe_read_fd) {
    @autoreleasepool {
        NSLog(@"[HUD] Process started. PID: %d", getpid());
        
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory]; // Hide from Dock
        
        setup_window();
        start_pipe_listener(pipe_read_fd);
        
        NSLog(@"[HUD] Entering main UI runloop...");
        [app run];
    }
    return 0;
}