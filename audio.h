#ifndef AUDIO_H_INCLUDED
#define AUDIO_H_INCLUDED

#include <stddef.h>
#include <stdbool.h>

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


typedef struct {
    float record_samples[CHUNK_SAMPLES];
    size_t sample_count;
    
    AudioQueueRef queue;
    AudioQueueBufferRef buffers[3];
    bool is_recording;
    
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

static void dispatch_current_chunk() {
    if (audio_state.sample_count > 0 && audio_state.on_chunk_ready) {
        // Дергаем функцию Оркестратора, передавая ей текущий массив и размер
        audio_state.on_chunk_ready(audio_state.record_samples, audio_state.sample_count);
        // Обнуляем счетчик, микрофон начнет писать с начала буфера
        audio_state.sample_count = 0;
    }
}

/**
 * Processes the audio data received from the microphone. This function is called by the AudioQueue when a buffer is filled.
 * It converts the PCM data to float, accumulates it in the internal buffer, and dispatches the chunk when it reaches the defined size.
 * @param userData: Pointer to the AudioState structure.
 * @param inAQ: The audio queue that called this callback.
 * @param inBuffer: The buffer containing the recorded audio data.
 * @param inStartTime: The time at which the audio data was recorded.
 * @param inNumberPacketDescriptions: The number of packet descriptions.
 * @param inPacketDescs: The packet descriptions for the audio data.
 */
static void audio_callback(void *userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer,
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
        // Если накопили ровно 10 секунд — сбрасываем чанк
        if (state->sample_count >= CHUNK_SAMPLES) {
            dispatch_current_chunk();
        }
        
        // Продолжаем запись
        state->record_samples[state->sample_count++] = (float)pcm[i] / 32768.0f;
    }

    // Возвращаем буфер системе на "карусель"
    if (state->is_recording) {
        AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, NULL);
    }
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