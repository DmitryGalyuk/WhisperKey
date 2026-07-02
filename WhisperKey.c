#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define TARGET_KEYCODE 176 // Кнопка микрофона

bool is_recording = false;
pid_t recording_pid = -1;
pid_t recognition_pid = -1;

#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon)
{
    CGKeyCode keycode = (CGKeyCode)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);

    if (keycode == TARGET_KEYCODE)
    {

        // Реагируем только на нажатие (KeyDown)
        if (type == kCGEventKeyDown)
        {

            // Защита от "залипания" (auto-repeat).
            // Если кнопку зажать, macOS шлет спам из KeyDown. Нам нужен только первый клик.
            int64_t is_repeat = CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat);
            if (is_repeat)
                return NULL;

            if (!is_recording)
            {
                // === НАЧАЛО ЗАПИСИ ===

                recording_pid = fork(); // Форкаем процесс

                if (recording_pid == 0)
                {
                    // --- Дочерний процесс ---
                    char *args[] = {"ffmpeg", "-f", "avfoundation", "-i", ":1", "-y",
                                    "./voice.wav", "-nostats", "-nostdin",
                                    //  "-loglevel", "0",
                                     NULL};

                    execvp(args[0], args);

                    // Если execvp провалился (например, ffmpeg не найден)
                    fprintf(stderr, "Ошибка: не удалось запустить %s\n", args[0]);
                    exit(1);
                }
                else if (recording_pid > 0)
                {
                    // --- Родительский процесс (наша программа) ---
                    printf("[СТАРТ] Запись пошла. PID рекордера: %d\n", recording_pid);
                    is_recording = true;
                }
            }
            else
            {
                // === ОСТАНОВКА ЗАПИСИ ===
                printf("[СТОП] Тормозим процесс %d...\n", recording_pid);

                // Отправляем сигнал SIGINT (это программный аналог нажатия Ctrl+C в терминале).
                // Важно слать именно его, а не SIGKILL, чтобы ffmpeg корректно дописал заголовки wav-файла.
                kill(recording_pid, SIGINT);

                // Ждем, пока процесс реально завершится, чтобы в системе не повис процесс-"зомби"
                waitpid(recording_pid, NULL, 0);

                printf("Файл сохранен! Запускаем распознавание...\n");

                is_recording = false;
                recording_pid = -1;

                recognition_pid = fork();
                if (recognition_pid == 0)
                {
                    // --- Дочерний процесс для распознавания ---
                    execvp("whisper-cli", (char *[]){"whisper-cli", "-m",
                                                     "/opt/homebrew/share/whisper-cpp/models/ggml-small.bin",
                                                     "-l", "ru", "-f", "./voice.wav",
                                                     "-otxt", "-mc", "0", "-et", "2.8", NULL});
                    return NULL;
                }
                else if (recognition_pid > 0)
                {
                    // --- Родительский процесс ---
                    // Ждем завершения распознавания, чтобы не запускать несколько одновременно
                    waitpid(recognition_pid, NULL, 0);
                    recognition_pid = -1;

                    // 2. Читаем файл и пишем в буфер обмена
                    // Самый элегантный способ в C под macOS — использовать popen() для вызова системной утилиты pbcopy.
                    FILE *txt_file = fopen("./voice.wav.txt", "r");
                    if (!txt_file)
                    {
                        perror("Не удалось найти файл с текстом");
                        return NULL;
                    }

                    // Открываем канал к утилите pbcopy в режиме записи ("w")
                    FILE *pbcopy = popen("pbcopy", "w");
                    if (!pbcopy)
                    {
                        perror("Не удалось запустить pbcopy");
                        fclose(txt_file);
                        return NULL;
                    }

                    // Перегоняем байтики из файла прямо в буфер обмена
                    char buffer[50];
                    size_t bytes_read;
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), txt_file)) > 0)
                    {
                        fwrite(buffer, 1, bytes_read, pbcopy);
                    }

                    pclose(pbcopy);
                    fclose(txt_file);
                    printf("[БУФЕР] Текст скопирован.\n");

                    // 3. Эмулируем нажатие Cmd + V для вставки под курсор
                    // Создаем источник событий (имитируем систему)
                    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);

                    // 9 — это сканкод клавиши 'V' на маке
                    CGEventRef v_down = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, true);
                    CGEventRef v_up = CGEventCreateKeyboardEvent(source, (CGKeyCode)9, false);

                    // Вешаем на эти нажатия зажатую кнопку Command (Cmd)
                    CGEventSetFlags(v_down, kCGEventFlagMaskCommand);
                    CGEventSetFlags(v_up, kCGEventFlagMaskCommand);

                    // Отправляем события в macOS
                    CGEventPost(kCGHIDEventTap, v_down);
                    CGEventPost(kCGHIDEventTap, v_up);

                    // Чистим за собой память
                    CFRelease(v_down);
                    CFRelease(v_up);
                    CFRelease(source);

                    printf("[ГОТОВО] Текст вставлен под курсор!\n");
                }
            }

            // Проглатываем нажатие, чтобы система на него не реагировала
            return NULL;
        }

        return event;
    }
}

int main()
{
    // Теперь мы слушаем только KeyDown
    CGEventMask eventMask = (1 << kCGEventKeyDown);

    CFMachPortRef eventTap = CGEventTapCreate(
        kCGSessionEventTap, kCGHeadInsertEventTap, 0, eventMask, eventCallback, NULL);

    if (!eventTap)
    {
        fprintf(stderr, "ОШИБКА: Дай права Универсального доступа терминалу.\n");
        return 1;
    }

    CFRunLoopSourceRef runLoopSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopCommonModes);
    CGEventTapEnable(eventTap, true);

    printf("Демон запущен (режим триггера). Жду F5...\n");
    CFRunLoopRun();

    return 0;
}