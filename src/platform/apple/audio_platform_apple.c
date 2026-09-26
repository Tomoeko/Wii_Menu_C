#include "audio_platform.h"

#include <AudioToolbox/AudioToolbox.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct WmAudioDevice {
    AudioComponentInstance unit;
    WmAudioRender render;
    void *context;
};

static OSStatus output_callback(void *reference,
                                AudioUnitRenderActionFlags *flags,
                                const AudioTimeStamp *time,
                                UInt32 bus,
                                UInt32 frames,
                                AudioBufferList *output)
{
    (void)flags;
    (void)time;
    (void)bus;
    WmAudioDevice *device = reference;
    if (output->mNumberBuffers != 1 ||
        output->mBuffers[0].mNumberChannels != 2 ||
        !output->mBuffers[0].mData ||
        output->mBuffers[0].mDataByteSize <
            (uint64_t)frames * 2u * sizeof(float)) {
        for (UInt32 index = 0; index < output->mNumberBuffers; index++) {
            if (output->mBuffers[index].mData) {
                memset(output->mBuffers[index].mData, 0,
                       output->mBuffers[index].mDataByteSize);
            }
        }
        return noErr;
    }
    device->render(device->context, output->mBuffers[0].mData, frames);
    return noErr;
}

WmAudioDevice *wm_audio_device_open(WmAudioRender render, void *context)
{
    if (!render) return NULL;
    WmAudioDevice *device = calloc(1, sizeof(*device));
    if (!device) return NULL;
    device->render = render;
    device->context = context;
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_DefaultOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple
    };
    AudioComponent component = AudioComponentFindNext(NULL, &description);
    AudioStreamBasicDescription format = {
        .mSampleRate = 48000.0,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = 2u * sizeof(float),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = 2u * sizeof(float),
        .mChannelsPerFrame = 2,
        .mBitsPerChannel = 8u * sizeof(float)
    };
    AURenderCallbackStruct callback = {
        .inputProc = output_callback,
        .inputProcRefCon = device
    };
    if (!component ||
        AudioComponentInstanceNew(component, &device->unit) != noErr ||
        AudioUnitSetProperty(device->unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &format,
                             sizeof(format)) != noErr ||
        AudioUnitSetProperty(device->unit, kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input, 0, &callback,
                             sizeof(callback)) != noErr ||
        AudioUnitInitialize(device->unit) != noErr ||
        AudioOutputUnitStart(device->unit) != noErr) {
        wm_audio_device_close(device);
        return NULL;
    }
    return device;
}

void wm_audio_device_close(WmAudioDevice *device)
{
    if (!device) return;
    if (device->unit) {
        AudioOutputUnitStop(device->unit);
        AudioUnitUninitialize(device->unit);
        AudioComponentInstanceDispose(device->unit);
    }
    free(device);
}
