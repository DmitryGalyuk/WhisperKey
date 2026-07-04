CC = clang
CFLAGS_COMMON = -Wall -Wextra -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lwhisper -framework Cocoa -framework CoreAudio -framework Carbon -framework ApplicationServices

TARGET = WhisperKey
SRCS = main.c engine.c hud.m

# По умолчанию собираем debug
all: debug

debug: CFLAGS = $(CFLAGS_COMMON) -O0 -g -DDEBUG
debug:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)
	@echo "[DEBUG] Сборка завершена: ./$(TARGET)"

release: CFLAGS = $(CFLAGS_COMMON) -O3 -flto -DNDEBUG
release:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)
	strip $(TARGET)
	@echo "[RELEASE] Сборка завершена: ./$(TARGET)"

clean:
	rm -f $(TARGET)
	rm -rf *.dSYM