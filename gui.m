#import <Cocoa/Cocoa.h>
#include <unistd.h>
#import <AVFoundation/AVFoundation.h>

#include "gui.h"
#include "logging.h"

NSWindow *overlayWindow;
NSTextField *emojiLabel;

int ipc_fd = -1;


void gui_send_command(const char *cmd) {
    if (ipc_fd != -1) {
        write(ipc_fd, cmd, strlen(cmd));
        write(ipc_fd, "\n", 1);
        LOG_DEBUG("Sent IPC command: %s", cmd);
    }
}

void gui_set_pipe(int fd) {
    ipc_fd = fd;
    LOG_DEBUG("IPC pipe set to FD: %d", ipc_fd);
}

void gui_hide() {
    gui_send_command("hide");
}

void gui_show(const char *emoji) {
    gui_send_command(emoji);
}

BOOL get_caret_position(NSPoint *outPoint) {
    AXUIElementRef systemWideElement = AXUIElementCreateSystemWide();
    AXUIElementRef focusedElement = NULL;
    BOOL success = NO;
    
    // 1. Получаем элемент в фокусе (активное текстовое поле)
    if (AXUIElementCopyAttributeValue(systemWideElement, kAXFocusedUIElementAttribute, (CFTypeRef *)&focusedElement) == kAXErrorSuccess) {
        
        AXValueRef rangeValue = NULL;
        // 2. Получаем выделенный диапазон (мигающий курсор - это диапазон длиной 0)
        if (AXUIElementCopyAttributeValue(focusedElement, kAXSelectedTextRangeAttribute, (CFTypeRef *)&rangeValue) == kAXErrorSuccess) {
            
            CFRange range;
            if (AXValueGetValue(rangeValue, kAXValueCFRangeType, &range)) {
                
                AXValueRef boundsValue = NULL;
                // 3. Запрашиваем экранные координаты (рамку) для этого курсора
                if (AXUIElementCopyParameterizedAttributeValue(focusedElement, kAXBoundsForRangeParameterizedAttribute, rangeValue, (CFTypeRef *)&boundsValue) == kAXErrorSuccess) {
                    
                    CGRect bounds;
                    if (AXValueGetValue(boundsValue, kAXValueCGRectType, &bounds)) {
                        
                        // Координаты AX (CoreGraphics) идут от левого ВЕРХНЕГО угла.
                        // Нам нужно перевернуть Y для AppKit (левый НИЖНИЙ угол).
                        CGFloat screenHeight = CGDisplayBounds(CGMainDisplayID()).size.height;
                        
                        // Берем нижнюю левую точку курсора и делаем отступ, 
                        // чтобы иконка HUD висела чуть правее и ниже самого текста
                        outPoint->x = bounds.origin.x + 10;
                        outPoint->y = screenHeight - bounds.origin.y - bounds.size.height - 20; 
                        
                        success = YES;
                    }
                    if (boundsValue) CFRelease(boundsValue);
                }
            }
            if (rangeValue) CFRelease(rangeValue);
        }
        if (focusedElement) CFRelease(focusedElement);
    }
    if (systemWideElement) CFRelease(systemWideElement);
    
    return success;
}

void gui_paste(const char *text) {
if (!text || strlen(text) == 0) return;

    NSPasteboard *pb = [NSPasteboard generalPasteboard];
    
    // 1. Читаем текущие элементы
    NSArray<NSPasteboardItem *> *oldItems = [pb pasteboardItems];
    NSMutableArray<NSPasteboardItem *> *clonedItems = [NSMutableArray array];
    
    // КЛОНИРУЕМ каждый элемент (глубокая копия данных), чтобы отвязать их от старого поколения буфера
    if (oldItems) {
        for (NSPasteboardItem *item in oldItems) {
            NSPasteboardItem *clonedItem = [[NSPasteboardItem alloc] init];
            for (NSPasteboardType type in item.types) {
                NSData *data = [item dataForType:type];
                if (data) {
                    [clonedItem setData:data forType:type];
                }
            }
            [clonedItems addObject:clonedItem];
        }
    }
    
    // 2. Кладем наш распознанный текст (это увеличит счетчик поколений буфера)
    [pb clearContents];
    [pb setString:[NSString stringWithUTF8String:text] forType:NSPasteboardTypeString];
    
    // 3. Эмулируем Cmd+V
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef vDown = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, true);
    CGEventSetFlags(vDown, kCGEventFlagMaskCommand);
    CGEventRef vUp = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, false);
    
    CGEventPost(kCGHIDEventTap, vDown);
    CGEventPost(kCGHIDEventTap, vUp);
    
    CFRelease(vDown);
    CFRelease(vUp);
    CFRelease(source);
    
    // 4. Ждем 50 мс, чтобы приложение успело переварить вставку
    usleep(50000); 
    
    // 5. Возвращаем наши КЛОНИРОВАННЫЕ элементы на место!
    [pb clearContents];
    if (clonedItems.count > 0) {
        [pb writeObjects:clonedItems];
    }
}


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

    [overlayWindow center];
    NSLog(@"[HUD] Window initialized successfully.");
}

void move_window_to_cursor() {
    NSPoint targetPoint;

    // Пытаемся прицепиться к текстовому курсору
    if (!get_caret_position(&targetPoint)) {
        // ФОЛБЭК: Если приложение скрывает курсор (например, не-нативный софт на Electron), 
        // падаем обратно на старую добрую позицию мыши.
        NSPoint mouseLoc = [NSEvent mouseLocation];
        targetPoint = NSMakePoint(mouseLoc.x + 20, mouseLoc.y - 80);
        NSLog(@"[HUD] Caret not found. Falling back to mouse coordinates.");
    } else {
        NSLog(@"[HUD] Caret found! Snapping to text.");
    }

    [overlayWindow setFrameOrigin:targetPoint];
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
            if ([command containsString:@"hide"]) {
                [overlayWindow orderOut:nil];
            }
            else {
                move_window_to_cursor();
                [emojiLabel setStringValue:command];
                [overlayWindow orderFront:nil];
            } 
        });
    });
    
    dispatch_resume(source);
}


void check_mic_permission() {
    LOG_INFO("[MAIN] Checking Microphone permissions...");
    if (@available(macOS 10.14, *)) {
        AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
        
        if (status == AVAuthorizationStatusAuthorized) {
            LOG_INFO("[MAIN] Microphone permission: GRANTED");
        } 
        else if (status == AVAuthorizationStatusNotDetermined) {
            LOG_INFO("[MAIN] Microphone permission: REQUESTING...");
            dispatch_semaphore_t sema = dispatch_semaphore_create(0);
            
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted) {
                if (granted) {
                    LOG_INFO("[MAIN] Microphone permission: GRANTED by user");
                } else {
                    LOG_ERROR("[MAIN ERROR] Microphone permission: DENIED by user");
                }
                dispatch_semaphore_signal(sema);
            }];
            
            // Ждем, пока пользователь не нажмет кнопку в системном окне
            dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
        } 
        else {
            LOG_ERROR("[MAIN ERROR] Microphone permission: DENIED. You must enable it in System Settings -> Privacy & Security -> Microphone");
        }
    }
}

int gui_run(int pipe_read_fd) {
    @autoreleasepool {
        LOG_INFO("[HUD] Process started. PID: %d", getpid());
        
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory]; // Hide from Dock
        
        setup_window();
        start_pipe_listener(pipe_read_fd);
        
        LOG_INFO("[HUD] Entering main UI runloop...");
        [app run];
    }
    return 0;
}
