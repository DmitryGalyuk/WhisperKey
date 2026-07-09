CC = clang
CFLAGS_COMMON = -Wall -Wextra -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lwhisper -lggml -framework Cocoa -framework CoreAudio -framework Carbon -framework ApplicationServices -framework AudioToolbox -framework AVFoundation -framework Accelerate

TARGET = bin/WhisperKey
SRCS = *.c *.m

APP_NAME = WhisperKey
APP_DIR = bin/$(APP_NAME).app
CONTENTS_DIR = $(APP_DIR)/Contents
MACOS_DIR = $(CONTENTS_DIR)/MacOS
RESOURCES_DIR = $(CONTENTS_DIR)/Resources

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


app: clean release
	@echo "📦 Building $(APP_NAME).app bundle..."
	@mkdir -p $(MACOS_DIR)
	@mkdir -p $(RESOURCES_DIR)
	@cp $(TARGET) $(MACOS_DIR)

	@if [ -f icon.png ]; then \
		echo "🎨 Generating icons from icon.png..."; \
		mkdir -p icon.iconset; \
		sips -z 16 16 icon.png --out icon.iconset/icon_16x16.png > /dev/null; \
		sips -z 32 32 icon.png --out icon.iconset/icon_16x16@2x.png > /dev/null; \
		sips -z 32 32 icon.png --out icon.iconset/icon_32x32.png > /dev/null; \
		sips -z 64 64 icon.png --out icon.iconset/icon_32x32@2x.png > /dev/null; \
		sips -z 128 128 icon.png --out icon.iconset/icon_128x128.png > /dev/null; \
		sips -z 256 256 icon.png --out icon.iconset/icon_128x128@2x.png > /dev/null; \
		sips -z 512 512 icon.png --out icon.iconset/icon_256x256@2x.png > /dev/null; \
		sips -z 1024 1024 icon.png --out icon.iconset/icon_512x512@2x.png > /dev/null; \
		iconutil -c icns icon.iconset -o $(RESOURCES_DIR)/AppIcon.icns > /dev/null; \
		rm -rf icon.iconset; \
		sips -z 18 18 icon.png --out $(RESOURCES_DIR)/icon.png > /dev/null; \
	fi
	
	@cp Info.plist $(CONTENTS_DIR)/Info.plist
	@echo "✅ Done! Application is ready at ./$(APP_DIR)"