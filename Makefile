CC = clang
CFLAGS_COMMON = -Wall -Wextra -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lwhisper -lggml -framework Cocoa -framework CoreAudio -framework Carbon -framework ApplicationServices -framework AudioToolbox -framework AVFoundation -framework Accelerate

TARGET = bin/WhisperKey
SRCS = *.c *.m

# By default, clean and build debug
all: clean debug

debug: CFLAGS = $(CFLAGS_COMMON) -O0 -g -DDEBUG
debug:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)
	@echo "[DEBUG] Build successful: ./$(TARGET)"

release: CFLAGS = $(CFLAGS_COMMON) -O3 -flto -DNDEBUG
release:
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)
	strip $(TARGET)
	@echo "[RELEASE] Build successful: ./$(TARGET)"

clean:
	rm -rf bin/*