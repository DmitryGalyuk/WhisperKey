#include <stdlib.h>
#include <AudioToolbox/AudioToolbox.h>

#include "logging.h"

#ifndef AUDIO_INCLUDED
#define AUDIO_INCLUDED

#define SAMPLE_RATE 16000
#define MAX_AUDIO_SEC 60
#define MAX_AUDIO_SAMPLES (SAMPLE_RATE * MAX_AUDIO_SEC)

// --- Audio State ---
typedef struct AudioState {
    float samples[MAX_AUDIO_SAMPLES];
    size_t sample_count;
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[3];
    bool is_recording;
} AudioState;


#endif // AUDIO_INCLUDED
// #define AUDIO_IMPLEMENTATION
#ifdef AUDIO_IMPLEMENTATION

void setup_default_microphone(AudioQueueRef queue);

#ifdef DEBUG

#include <stdint.h>

void save_wav_debug(const char *filename, const float *samples, size_t count, int sample_rate) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        LOG_ERROR("Failed to open %s for writing debug audio", filename);
        return;
    }

    int channels = 1;
    int bits_per_sample = 16;
    int byte_rate = sample_rate * channels * (bits_per_sample / 8);
    int block_align = channels * (bits_per_sample / 8);
    int data_size = count * channels * (bits_per_sample / 8);
    int chunk_size = 36 + data_size;

    // RIFF header
    fwrite("RIFF", 1, 4, f);
    fwrite(&chunk_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    // fmt subchunk
    fwrite("fmt ", 1, 4, f);
    int subchunk1_size = 16;
    short audio_format = 1; // PCM
    fwrite(&subchunk1_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    // data subchunk
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);

    // Convert float samples back to 16-bit PCM and write
    for (size_t i = 0; i < count; i++) {
        // Умножаем float [-1.0, 1.0] обратно в диапазон int16
        float scaled = samples[i] * 32767.0f;
        // Защита от клиппинга
        if (scaled > 32767.0f) scaled = 32767.0f;
        if (scaled < -32768.0f) scaled = -32768.0f;
        
        int16_t sample = (int16_t)scaled;
        fwrite(&sample, 2, 1, f);
    }

    fclose(f);
    LOG_INFO("Saved debug audio to %s (%.2f seconds)", filename, (float)count / sample_rate);
}
#endif // DEBUG

// --- Audio Capture ---
void audio_callback(void *userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer,
                    const AudioTimeStamp *inStartTime, UInt32 inNumberPacketDescriptions,
                    const AudioStreamPacketDescription *inPacketDescs) {
    (void)inStartTime;
    (void)inNumberPacketDescriptions;
    (void)inPacketDescs;

    AudioState *state = (AudioState*)userData;
    if (!state->is_recording) return;

    int16_t *pcm = (int16_t*)inBuffer->mAudioData;
    UInt32 num_samples = inBuffer->mAudioDataByteSize / sizeof(int16_t);

    for (UInt32 i = 0; i < num_samples; i++) {
        if (state->sample_count < MAX_AUDIO_SAMPLES) {
            // Convert 16-bit int to 32-bit float for Whisper
            state->samples[state->sample_count++] = (float)pcm[i] / 32768.0f;
        } else {
            // Buffer full! 60 seconds reached.
            if (state->is_recording) {
                LOG_INFO("Max recording time reached (60s). Auto-stopping.");
                // We cannot call stop_recording here directly because it's the audio thread.
                // We just stop accepting data.
                state->is_recording = false; 
            }
            break;
        }
    }

    if (state->is_recording) {
        AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
    }
}

void audio_start_recording(AudioState *audio_state) {
    audio_state->sample_count = 0;
    audio_state->is_recording = true;

    AudioStreamBasicDescription format = {0};
    format.mSampleRate = 16000.0; // Float64 для CoreAudio
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 1; 
    format.mBitsPerChannel = 16;
    format.mBytesPerPacket = 2;
    format.mBytesPerFrame = 2;

    OSStatus status;

    // ИСПРАВЛЕНО: NULL, NULL для правильного создания внутреннего потока
    status = AudioQueueNewInput(&format, audio_callback, audio_state, CFRunLoopGetCurrent(), kCFRunLoopCommonModes, 0, &audio_state->queue);
    if (status != noErr) {
        LOG_ERROR("AudioQueueNewInput failed. OSStatus: %d", (int)status);
        return;
    }

    setup_default_microphone(audio_state->queue);

    UInt32 buffer_size = (SAMPLE_RATE * 2) / 10; 
    for (int i = 0; i < 3; i++) {
        status = AudioQueueAllocateBuffer(audio_state->queue, buffer_size, &audio_state->buffers[i]);
        if (status != noErr) LOG_ERROR("Buffer %d allocation failed: %d", i, (int)status);
        
        status = AudioQueueEnqueueBuffer(audio_state->queue, audio_state->buffers[i], 0, NULL);
        if (status != noErr) LOG_ERROR("Buffer %d enqueue failed: %d", i, (int)status);
    }

    status = AudioQueueStart(audio_state->queue, NULL);
    if (status != noErr) {
        LOG_ERROR("AudioQueueStart failed. OSStatus: %d", (int)status);
    } else {
        LOG_INFO("AudioQueue hardware started successfully.");
    }
}

void audio_stop_recording(AudioState *audio_state) {
    LOG_INFO("Stopping audio capture...");
    if (!audio_state->is_recording && audio_state->queue == NULL) return;
    
    audio_state->is_recording = false;
    
    // Stop and clean up CoreAudio
    if (audio_state->queue) {
        AudioQueueStop(audio_state->queue, true);
        AudioQueueDispose(audio_state->queue, true);
        audio_state->queue = NULL;
    }
    LOG_INFO("Audio capture stopped. Recorded %zu samples.", audio_state->sample_count);

    #ifdef DEBUG
    save_wav_debug("debug_recording.wav", audio_state->samples, audio_state->sample_count, SAMPLE_RATE);
    #endif
}

// Если захочешь привязаться к конкретному микрофону, впиши сюда его UID из логов.
// Оставь NULL, чтобы использовать системный по умолчанию.
#define TARGET_MIC_UID NULL // пример: "BuiltInMicrophoneDevice"

void setup_default_microphone(AudioQueueRef queue) {
    AudioDeviceID defaultDeviceID = kAudioObjectUnknown;
    UInt32 size = sizeof(defaultDeviceID);
    
    // Спрашиваем систему: "Какой у тебя микрофон по умолчанию?"
    AudioObjectPropertyAddress defaultAddr = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, NULL, &size, &defaultDeviceID) != noErr) {
        LOG_ERROR("Failed to get default input device from system.");
        return;
    }

    // Получаем UID этого устройства, чтобы скормить его в AudioQueue
    CFStringRef deviceUID = NULL;
    size = sizeof(deviceUID);
    AudioObjectPropertyAddress uidAddr = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    if (AudioObjectGetPropertyData(defaultDeviceID, &uidAddr, 0, NULL, &size, &deviceUID) == noErr && deviceUID != NULL) {
        
        // (Опционально) Получаем красивое имя для логов
        CFStringRef deviceName = NULL;
        UInt32 nameSize = sizeof(deviceName);
        AudioObjectPropertyAddress nameAddr = {
            kAudioDevicePropertyDeviceNameCFString,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        
        char nameBuf[256] = "Unknown Device";
        if (AudioObjectGetPropertyData(defaultDeviceID, &nameAddr, 0, NULL, &nameSize, &deviceName) == noErr && deviceName != NULL) {
            CFStringGetCString(deviceName, nameBuf, sizeof(nameBuf), kCFStringEncodingUTF8);
            CFRelease(deviceName);
        }

        LOG_INFO(">>> Binding AudioQueue to System Default Mic: %s", nameBuf);

        // Жестко привязываем очередь к системному микрофону
        OSStatus err = AudioQueueSetProperty(queue, kAudioQueueProperty_CurrentDevice, &deviceUID, sizeof(deviceUID));
        if (err != noErr) {
            LOG_ERROR("Failed to bind microphone. OSStatus: %d", (int)err);
        } else {
            LOG_INFO("Successfully bound AudioQueue to: %s", nameBuf);
        }
        
        CFRelease(deviceUID);
    } else {
        LOG_ERROR("Could not get UID for the default microphone.");
    }
}

#endif // AUDIO_IMPLEMENTATION