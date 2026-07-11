#ifndef AUDIO_H_INCLUDED
#define AUDIO_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>
#include <math.h>

/**
 * To provide the business logic once chunk of audio is ready, the audio module will call this callback function.
 * @param samples: array with data from microphone to be processed by caller
 * @param sample_count: Number of samples in the array.
 */
typedef void (*AudioChunkReadyCallback)(const float *samples, size_t sample_count);

void audio_init(AudioChunkReadyCallback callback);

void audio_start_recording(void);
void audio_stop_recording(void);
bool audio_is_recording(void);

#endif // AUDIO_H_INCLUDED


#ifdef AUDIO_IMPLEMENTATION

#include <AudioToolbox/AudioToolbox.h>
#include <string.h>
#include "logging.h"

#define SAMPLE_RATE 16000
#define CHUNK_SEC 10
#define CHUNK_SAMPLES (SAMPLE_RATE * CHUNK_SEC)

#define MIN_CHUNK_SAMPLES (SAMPLE_RATE * 5) // minimum 3 seconds
#define MAX_CHUNK_SAMPLES (SAMPLE_RATE * 10) // maximum 10 seconds
#define SILENCE_THRESHOLD 0.005f // audio level below which we consider it silence
#define SILENCE_FRAMES_REQ 5 // duration of the silence in frames (each frame is ~100ms) to consider the chunk complete


typedef struct {
    float record_samples[CHUNK_SAMPLES];
    size_t sample_count;
    
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[3];
    bool is_recording;

    int silence_frames;
    
    AudioChunkReadyCallback on_chunk_ready;
} AudioState;

/**
 * Internal state of the audio module. It OWNS the recording buffer, the audio queue, and the callback to notify when a chunk is ready.
 */
static AudioState audio_state = {0};


static void setup_default_microphone(AudioQueueRef queue) {
    AudioDeviceID defaultDeviceID = kAudioObjectUnknown;
    UInt32 size = sizeof(defaultDeviceID);
    
    AudioObjectPropertyAddress defaultAddr = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultAddr, 0, NULL, &size, &defaultDeviceID) != noErr) {
        LOG_ERROR("Failed to get default input device.");
        return;
    }

    CFStringRef deviceUID = NULL;
    size = sizeof(deviceUID);
    AudioObjectPropertyAddress uidAddr = {
        kAudioDevicePropertyDeviceUID,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    if (AudioObjectGetPropertyData(defaultDeviceID, &uidAddr, 0, NULL, &size, &deviceUID) == noErr && deviceUID != NULL) {
        OSStatus err = AudioQueueSetProperty(queue, kAudioQueueProperty_CurrentDevice, &deviceUID, sizeof(deviceUID));
        if (err != noErr) {
            LOG_ERROR("Failed to bind microphone. OSStatus: %d", (int)err);
        } else {
            LOG_INFO("Successfully bound AudioQueue to system mic.");
        }
        CFRelease(deviceUID);
    }
}

bool is_silent_audio_chunk(const float *samples, size_t sample_count) {
    if (sample_count == 0) return true;

    float sum_squares = 0.0f;
    for (size_t i = 0; i < sample_count; i++) {
        sum_squares += samples[i] * samples[i];
    }
    float rms = sqrtf(sum_squares / sample_count);
    
    if (rms < SILENCE_THRESHOLD) {
        LOG_INFO("Detected silent audio chunk. RMS: %.5f", rms);
    }

    return rms < SILENCE_THRESHOLD;
}

static void dispatch_current_chunk() {
    // only send non-silent chunks to the application logic
    LOG_DEBUG("[AUDIO] trying to dispatch %zu samples", audio_state.sample_count);
    if (audio_state.sample_count > 0 
        && audio_state.on_chunk_ready
        && !is_silent_audio_chunk(audio_state.record_samples, audio_state.sample_count)
        ){
            LOG_DEBUG("[AUDIO] not silent - sending");
            audio_state.on_chunk_ready(audio_state.record_samples, audio_state.sample_count);
    }
    // reset the samples count since we processed the chunk
    audio_state.sample_count = 0;
}

/**
 * Processes the audio data received from the microphone. This function is called by the AudioQueue when a buffer is filled.
 */
static void audio_callback(
    void *inUserData, 
    AudioQueueRef inAQ, 
    AudioQueueBufferRef inBuffer, 
    const AudioTimeStamp *inStartTime, 
    UInt32 inNumberPacketDescriptions, 
    const AudioStreamPacketDescription *inPacketDescs) 
{
    (void) inStartTime;
    (void) inNumberPacketDescriptions;
    (void) inPacketDescs;

    AudioState *state = (AudioState *)inUserData;
    
    if (!state->is_recording) return;

    int16_t *pcm = (int16_t*)inBuffer->mAudioData;
    UInt32 num_samples = inBuffer->mAudioDataByteSize / sizeof(int16_t);

    float sum_squares = 0.0f;

    // 1. ЦИКЛ ПО СЭМПЛАМ (с защитой от переполнения)
    for (UInt32 i = 0; i < num_samples; i++) {
        
        // --- ЖЕСТКИЙ ЛИМИТ (HARD LIMIT) ---
        if (state->sample_count >= CHUNK_SAMPLES) {
            dispatch_current_chunk(); // Внутри сам сделает state->sample_count = 0
            state->silence_frames = 0;
        }

        // Конвертируем и безопасно пишем сэмпл
        float sample = (float)pcm[i] / 32768.0f;
        sum_squares += sample * sample;
        state->record_samples[state->sample_count++] = sample;
    }

    // 2. ЛОГИКА ПО ТИШИНЕ (SOFT LIMIT)
    float rms = sqrtf(sum_squares / num_samples);
    
    if (rms < SILENCE_THRESHOLD) {
        state->silence_frames++;
    } else {
        state->silence_frames = 0;
    }

    // Если наговорили минимальный кусок И наступила тишина — отрезаем чанк
    if (state->sample_count >= MIN_CHUNK_SAMPLES && state->silence_frames >= SILENCE_FRAMES_REQ) {
        dispatch_current_chunk();
        state->silence_frames = 0;
    }

    // ВАЖНО: Возвращаем отработанный буфер обратно в очередь CoreAudio!
    AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
}

/**
 * Initializes the audio module and sets up the callback to be called when a chunk of audio is ready for processing.
 * @param callback: Function pointer to be called when a chunk of audio is ready by the application logic.
 */
void audio_init(AudioChunkReadyCallback callback) {
    memset(&audio_state, 0, sizeof(audio_state));
    audio_state.on_chunk_ready = callback;
    LOG_INFO("Audio module initialized.");
}

void audio_start_recording(void) {
    if (audio_state.is_recording) return;

    audio_state.sample_count = 0;
    audio_state.silence_frames = 0;
    audio_state.is_recording = true;

    AudioStreamBasicDescription format = {0};
    format.mSampleRate = 16000.0;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 1; 
    format.mBitsPerChannel = 16;
    format.mBytesPerPacket = 2;
    format.mBytesPerFrame = 2;

    OSStatus status = AudioQueueNewInput(&format, audio_callback, &audio_state, 
                                         CFRunLoopGetCurrent(), kCFRunLoopCommonModes, 0, 
                                         &audio_state.queue);
    
    if (status != noErr) {
        LOG_ERROR("AudioQueueNewInput failed: %d", (int)status);
        audio_state.is_recording = false;
        return;
    }

    setup_default_microphone(audio_state.queue);

    UInt32 buffer_size = (SAMPLE_RATE * 2) / 10; // ~100ms
    for (int i = 0; i < 3; i++) {
        AudioQueueAllocateBuffer(audio_state.queue, buffer_size, &audio_state.buffers[i]);
        AudioQueueEnqueueBuffer(audio_state.queue, audio_state.buffers[i], 0, NULL);
    }

    status = AudioQueueStart(audio_state.queue, NULL);
    if (status == noErr) {
        LOG_INFO("Audio hardware capture started.");
    } else {
        LOG_ERROR("AudioQueueStart failed: %d", (int)status);
        audio_state.is_recording = false;
    }
}

void audio_stop_recording(void) {
    if (!audio_state.is_recording) return;
    
    audio_state.is_recording = false;
    
    if (audio_state.queue) {
        AudioQueueStop(audio_state.queue, true);
        AudioQueueDispose(audio_state.queue, true);
        audio_state.queue = NULL;
    }
    
    LOG_INFO("Audio capture stopped.");
    
    // Выталкиваем последний незавершенный хвостик аудио
    dispatch_current_chunk();
}

bool audio_is_recording(void) {
    return audio_state.is_recording;
}

#endif // AUDIO_IMPLEMENTATION