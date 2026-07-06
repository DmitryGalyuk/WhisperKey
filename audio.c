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
    format.mSampleRate = SAMPLE_RATE;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 1; // Mono
    format.mBitsPerChannel = 16;
    format.mBytesPerPacket = 2;
    format.mBytesPerFrame = 2;

    AudioQueueNewInput(&format, audio_callback, audio_state, NULL, kCFRunLoopCommonModes, 0, audio_state->queue);

    // 3 buffers, each ~100ms
    UInt32 buffer_size = (SAMPLE_RATE * 2) / 10; 
    for (int i = 0; i < 3; i++) {
        AudioQueueAllocateBuffer(audio_state->queue, buffer_size, audio_state->buffers[i]);
        AudioQueueEnqueueBuffer(audio_state->queue, audio_state->buffers[i], 0, NULL);
    }

    AudioQueueStart(audio_state->queue, NULL);
    LOG_INFO("Audio capture started.");
}

void audio_stop_recording(AudioState *audio_state) {
    if (!audio_state->is_recording && audio_state->queue == NULL) return;
    
    audio_state->is_recording = false;
    
    // Stop and clean up CoreAudio
    if (audio_state->queue) {
        AudioQueueStop(audio_state->queue, true);
        AudioQueueDispose(audio_state->queue, true);
        audio_state->queue = NULL;
    }
    LOG_INFO("Audio capture stopped. Recorded %zu samples.", audio_state->sample_count);
}

#endif // AUDIO_IMPLEMENTATION