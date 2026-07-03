# WhisperKey

A lightweight, background macOS daemon that hijacks the native microphone/dictation button (key code 176) to record audio, transcribe it locally using AI, and instantly paste the text into your active window.

Zero UI clutter. Pure C. Relies on native macOS accessibility hooks and system audio feedback.

## Features
* **Global Hotkey:** Listens for the hardware microphone button out of the box.
* **Local Processing:** Audio is transcribed entirely offline.
* **Audio Feedback:** Uses native macOS system sounds (`afplay`) to signal recording start, processing, and successful paste.
* **Clean & Async:** Background threads handle the heavy lifting (FFmpeg and AI inference) without blocking the macOS event loop.
* **Auto-Cleanup:** Temporary `.wav` and `.txt` files are automatically deleted after insertion.

## Dependencies

You need to have `ffmpeg` and `whisper.cpp` installed on your Mac. The easiest way is via Homebrew:

```bash
brew install ffmpeg whisper-cpp
```

**Note**: You also need a Whisper model (e.g., ggml-medium.bin or ggml-small.bin). You can download them directly from the official HuggingFace repository.

## Building
Compile the C code using gcc or clang. The only required framework is ApplicationServices for the keyboard hooks.

```bash
gcc main.c -o WhisperKey -framework ApplicationServices
```

## Usage
Run the compiled binary from the terminal, passing the absolute path to your downloaded Whisper model as the only argument:

```bash
./WhisperKey /path/to/your/models/ggml-small.bin
```

## Important: Accessibility Permissions
On the first run, macOS will block the event tap. You must grant Accessibility permissions to your Terminal app (or the compiled binary):

Go to System Settings -> Privacy & Security -> Accessibility.

Add/Enable your Terminal (e.g., iTerm2, Alacritty, or default Terminal).

Restart the app.

## How It Works
Press the microphone key. You will hear a short Tink sound.

Speak your text.

Press the microphone key again to stop. You will hear a Pop sound.

Whisper processes the audio in the background.

The transcribed text is pasted into your currently focused text field via a simulated Cmd+V, followed by a Glass sound.
