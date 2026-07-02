#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <pthread.h> // Подключаем библиотеку потоков

#define TARGET_KEYCODE 176 // Кнопка микрофона

bool is_recording = false;
pid_t recording_pid = -1;

// === ЭТОТ КОД РАБОТАЕТ В НЕВИДИМОМ ФОНОВОМ ПОТОКЕ ===
void* process_audio_thread(void* arg) {
    // Получаем PID ffmpeg, который нужно остановить
    pid_t pid_to_kill = (pid_t)(uintptr_t)arg;

    printf("[СТОП] Тормозим ffmpeg (PID: %d)...\n", pid_to_kill);
    
    // 1. Посылаем сигнал завершения
    kill(pid_to_kill, SIGINT);
    
    // 2. Ждем сохранения файла. Теперь это не блокирует клавиатуру!
    waitpid(pid_to_kill, NULL, 0);

    printf("Файл сохранен! Запускаем Whisper...\n");

    // 3. Запускаем распознавание
    system("whisper-cli -m /opt/homebrew/share/whisper-cpp/models/ggml-small.bin -l ru -f ./voice.wav -otxt -mc 0 -et 2.8 > /dev/null 2>&1");

    // 4. Копируем результат и вставляем
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
        printf("[БУФЕР] Текст скопирован.\n");

        // Эмулируем Cmd+V
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
        
        printf("[ГОТОВО] Текст вставлен!\n");
    } else {
        printf("[ОШИБКА] Файл с текстом не найден.\n");
    }
    
    return NULL;
}
// ===================================================

CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon) {
    // На всякий случай оставляем авто-восстановление, если макось решит пошалить
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        CGEventTapEnable(proxy, true);
        return event;
    }

    CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

    if (keycode == TARGET_KEYCODE && type == kCGEventKeyDown) {
        
        int64_t is_repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat);
        if (is_repeat) return NULL;

        if (!is_recording) {
            // === НАЧАЛО ЗАПИСИ ===
            recording_pid = fork(); 

            if (recording_pid == 0) {
                char *args[] = {"ffmpeg", "-f", "avfoundation", "-i", ":1", "-y",
                                "./voice.wav", "-nostats", "-nostdin", "-loglevel", "0", NULL};
                execvp(args[0], args);
                exit(1);
            } else if (recording_pid > 0) {
                printf("[СТАРТ] Запись пошла. PID: %d\n", recording_pid);
                is_recording = true;
            }
        } else {
            // === ОСТАНОВКА И ОБРАБОТКА ===
            // Мы больше не ждем ffmpeg в главном потоке!
            is_recording = false;
            pid_t pid_to_kill = recording_pid;
            recording_pid = -1; // Сразу сбрасываем PID для будущих записей

            // Создаем фоновый поток и отдаем ему всю грязную работу
            pthread_t worker_thread;
            pthread_create(&worker_thread, NULL, process_audio_thread, (void*)(uintptr_t)pid_to_kill);
            
            // Отвязываем поток, чтобы система сама зачистила его после завершения
            pthread_detach(worker_thread);
        }

        return NULL; // Глотаем нажатие и МОМЕНТАЛЬНО возвращаем управление системе
    }

    return event;
}

int main() {
    CGEventMask eventMask = (1 << kCGEventKeyDown);

    CFMachPortRef eventTap = CGEventTapCreate(
        kCGSessionEventTap, kCGHeadInsertEventTap, 0, eventMask, eventCallback, NULL);

    if (!eventTap) {
        fprintf(stderr, "ОШИБКА: Дай права Универсального доступа терминалу.\n");
        return 1;
    }

    CFRunLoopSourceRef runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes);
    CGEventTapEnable(eventTap, true);

    printf("Демон запущен (Версия с потоками). Жду кнопку диктовки...\n");
    CFRunLoopRun();

    return 0;
}