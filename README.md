# WhisperKey

A local, lightweight, and fast voice dictation tool for macOS. It lives in your menu bar, triggers via native Microphone hotkey, and converts speech to text directly on your device using neural networks (`whisper.cpp` / `ggml`).

No cloud processing, no subscriptions, and no data leaks. Written in pure C and Objective-C.

## Features

* **Fully Local:** Your audio never leaves your Mac. Everything is processed on-device.
* **Smart VAD (Voice Activity Detection):** Chunks speech based on natural pauses rather than hard timers. This ensures better context and accurate punctuation.
* **High Performance:** Built with thread-safe Ring Buffers and native APIs (CoreAudio, AppKit) to keep system impact close to zero. The audio thread never blocks.
* **Low Memory Footprint:** The UI and engine run in isolated processes. To save RAM, quantized models are highly recommended.

## Requirements

* macOS (optimized for Apple Silicon / M-series).
* Xcode Command Line Tools. If you don't have them, run:
```bash
xcode-select --install

```


* `whisper.cpp` and `ggml` libraries (can be installed via Homebrew).

## Build Instructions

Since the app is not signed with a paid Apple Developer certificate (which would trigger Gatekeeper warnings), you need to build it locally. The process takes less than a minute.

1. Clone the repository:
```bash
git clone https://github.com/YOUR_ACCOUNT/WhisperKey.git
cd WhisperKey

```


2. Download a Whisper model. A quantized `medium` model (e.g., `ggml-medium-q5_1.bin`) is highly recommended—it provides excellent accuracy while using only ~550 MB of RAM.
*Place the model in the path specified in the source code (default is `/opt/homebrew/share/whisper-cpp/models/ggml-medium.bin`—update the path in the code if yours is located elsewhere).*
3. Build the macOS app bundle:
```bash
make app

```



The compiled `WhisperKey.app` will appear in the `bin/` directory.

## Usage

1. Run `WhisperKey.app` from the `bin/` folder.
2. On the first launch, macOS will request two permissions required for the app to function:
* **Microphone** — to capture your voice.
* **Accessibility** — to automatically paste the recognized text into your active window.


3. A microphone icon will appear in your menu bar.
4. Press the global hotkey *(insert your hotkey here, e.g., Cmd + Option + Space)* to start recording. The icon will change to indicate active recording.
5. Dictate your text, then press the hotkey again to stop. The recognized text will be instantly typed wherever your cursor is currently active.

