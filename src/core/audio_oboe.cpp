/*
 * audio_oboe.cpp - Dire Wolf's audio.h on Android, through Oboe.
 *
 * Replaces audio.c (ALSA) on Android.  Direwolf pulls and pushes audio one
 * octet at a time through audio_get() and audio_put(); this backend keeps a
 * block buffer per direction and moves whole blocks through Oboe's blocking
 * read() and write(), which is the simplest arrangement that never starves
 * the demodulator: the audio input thread of recv.c blocks in read() and
 * the transmit thread blocks in write(), exactly as they do on ALSA.  The
 * input is opened on Android's normal path with a second of buffer, like
 * the large buffer ALSA gives on the desktop, so the time the demodulator
 * spends between two reads never costs audio; overruns, if any, are told
 * in the modem log.  The output stays low-latency for a tight PTT.
 *
 * Only real devices: "stdin" and "udp:" inputs of the desktop are not
 * available.  ADEVICE takes "default" or a numeric Android audio device id.
 * The sample rate of the configuration is honoured through Oboe's own
 * resampler, so a 44100 Hz direwolf.conf works on a 48000 Hz phone.
 *
 * This file is part of the AX25Chess embedded Direwolf core.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

// <cstring> first: some Oboe headers use memset without including it.
#include <cstring>
#include <oboe/Oboe.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <algorithm>
#include <chrono>

// direwolf.h declares strcasestr() the C way, which collides with the C++
// overloads a libc's <string.h> may provide; the declaration is renamed
// away, as DirewolfHeaders.h does for the Qt side.
#define strcasestr dw_strcasestr_decl_unused
extern "C" {
#include "direwolf.h"
#include "audio.h"
#include "textcolor.h"
}
#undef strcasestr

namespace {

constexpr int BlockBytes = 4096;
constexpr int64_t ReadTimeoutNs = 2000000000LL;    // 2 s: a stalled stream reports an error
constexpr int64_t WriteTimeoutNs = 5000000000LL;
// The input keeps this much audio while the demodulator is busy elsewhere,
// see openStream().  Room, not delay: a reader that keeps up finds it empty.
constexpr int InputCapacityMs = 1000;
// How often the input's overrun counter is looked at.
constexpr std::chrono::seconds XrunCheckPeriod{5};

struct Device {
    std::shared_ptr<oboe::AudioStream> in;
    std::shared_ptr<oboe::AudioStream> out;
    int channels = 1;
    int bytesPerFrame = 2;
    int rate = 48000;
    int32_t inDevice = oboe::kUnspecified;
    int32_t outDevice = oboe::kUnspecified;
    unsigned char inbuf[BlockBytes];
    int inLen = 0;
    int inPos = 0;
    unsigned char outbuf[BlockBytes];
    int outLen = 0;
    int64_t framesWritten = 0;
    // An input stream that failed (the phone locked, a route changed, the
    // USB card went away for a moment) is reopened quietly while silence
    // feeds the demodulator; the receiver must never see an end of stream.
    bool inBroken = false;
    int inRetries = 0;
    std::chrono::steady_clock::time_point inNextRetry;
    // Overruns of the current input stream already reported.
    int32_t inXruns = 0;
    std::chrono::steady_clock::time_point inXrunCheck;
    bool outBroken = false;
    std::chrono::steady_clock::time_point outNextRetry;
};

Device g_dev[MAX_ADEVS];
struct audio_s *g_config = nullptr;

// The time one block of audio represents, for pacing silence.
std::chrono::microseconds blockDuration(const Device &d)
{
    const int frames = BlockBytes / d.bytesPerFrame;
    return std::chrono::microseconds(static_cast<int64_t>(frames) * 1000000 / d.rate);
}

int32_t deviceId(const char *name)
{
    if (name == nullptr || name[0] == '\0' || std::strcmp(name, "default") == 0 || std::strcmp(name, "0") == 0) {
        return oboe::kUnspecified;
    }
    char *end = nullptr;
    const long id = std::strtol(name, &end, 10);
    return (end && *end == '\0' && id > 0) ? static_cast<int32_t>(id) : oboe::kUnspecified;
}

std::shared_ptr<oboe::AudioStream> openStream(oboe::Direction direction, int32_t device, int channels, int rate, const char *label)
{
    oboe::AudioStreamBuilder builder;
    builder.setDirection(direction)
        ->setSharingMode(oboe::SharingMode::Shared)
        ->setFormat(oboe::AudioFormat::I16)
        ->setFormatConversionAllowed(true)
        ->setChannelCount(channels)
        ->setSampleRate(rate)
        ->setSampleRateConversionQuality(oboe::SampleRateConversionQuality::Medium)
        ->setDeviceId(device);
    if (direction == oboe::Direction::Input) {
        // The receiver is Dire Wolf's own thread: it reads one block (43 ms
        // at 48 kHz), demodulates it, and only then reads again.  A
        // low-latency input keeps a few milliseconds of audio (a FAST track
        // or an MMAP buffer, Oboe's AudioStreamAAudio.cpp says as much): the
        // time spent demodulating a block, or any pause of the thread on a
        // phone that slows its CPU down when idle, and the phone drops the
        // audio that arrived meanwhile.  At 1200 baud a bit lasts 0.83 ms, so
        // a gap of a few ms costs the frame its CRC - the frames of the other
        // station were lost, while the desktop, where ALSA keeps a buffer
        // many periods long, decoded them all.  A packet receiver has no use
        // for low latency: the normal path, with a second of room.
        builder.setPerformanceMode(oboe::PerformanceMode::None)
            ->setBufferCapacityInFrames(rate * InputCapacityMs / 1000)
            ->setInputPreset(oboe::InputPreset::Unprocessed);   // no AGC, no noise suppression on packet audio
    } else {
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    }
    std::shared_ptr<oboe::AudioStream> stream;
    const oboe::Result result = builder.openStream(stream);
    if (result != oboe::Result::OK) {
        text_color_set(DW_COLOR_ERROR);
        dw_printf("Could not open the %s audio stream: %s\n", label, oboe::convertToText(result));
        return nullptr;
    }
    const oboe::Result started = stream->requestStart();
    if (started != oboe::Result::OK) {
        text_color_set(DW_COLOR_ERROR);
        dw_printf("Could not start the %s audio stream: %s\n", label, oboe::convertToText(started));
        stream->close();
        return nullptr;
    }
    const int capacity = stream->getBufferCapacityInFrames();
    text_color_set(DW_COLOR_INFO);
    dw_printf("Audio %s: device %d, %d Hz, %d channel(s), %s, %s mode, buffer %d ms.\n", label, stream->getDeviceId(),
              stream->getSampleRate(), stream->getChannelCount(),
              stream->getAudioApi() == oboe::AudioApi::AAudio ? "AAudio" : "OpenSL ES",
              stream->getPerformanceMode() == oboe::PerformanceMode::LowLatency ? "low-latency"
              : stream->getPerformanceMode() == oboe::PerformanceMode::PowerSaving ? "power-saving" : "normal",
              capacity > 0 && stream->getSampleRate() > 0 ? static_cast<int>(1000LL * capacity / stream->getSampleRate()) : 0);
    return stream;
}

} // namespace


extern "C" int audio_open(struct audio_s *pa)
{
    g_config = pa;
    for (int a = 0; a < MAX_ADEVS; a++) {
        Device &d = g_dev[a];
        d = Device();
        if (!pa->adev[a].defined) continue;

        if (pa->adev[a].bits_per_sample != 16) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf("Only 16 bit audio is available on Android; ignoring the configured %d bits.\n", pa->adev[a].bits_per_sample);
            pa->adev[a].bits_per_sample = 16;
        }
        d.channels = pa->adev[a].num_channels == 2 ? 2 : 1;
        d.rate = pa->adev[a].samples_per_sec > 0 ? pa->adev[a].samples_per_sec : 48000;
        d.bytesPerFrame = 2 * d.channels;

        d.inDevice = deviceId(pa->adev[a].adevice_in);
        d.outDevice = deviceId(pa->adev[a].adevice_out);
        d.in = openStream(oboe::Direction::Input, d.inDevice, d.channels, d.rate, "input");
        if (!d.in) return -1;
        d.out = openStream(oboe::Direction::Output, d.outDevice, d.channels, d.rate, "output");
        if (!d.out) {
            d.in->close();
            d.in.reset();
            return -1;
        }
    }
    return 0;
}

// The input stream failed: drop it and try to open a fresh one, first at
// once, then once a second.  True when a stream is up again.
static bool recoverInput(Device &d, const char *why)
{
    const auto now = std::chrono::steady_clock::now();
    if (!d.inBroken) {
        d.inBroken = true;
        d.inRetries = 0;
        d.inNextRetry = now;
        text_color_set(DW_COLOR_ERROR);
        dw_printf("Audio input error: %s. Reopening the input; silence meanwhile.\n", why);
        if (d.in) { d.in->requestStop(); d.in->close(); d.in.reset(); }
    }
    if (now < d.inNextRetry) return false;
    d.inNextRetry = now + std::chrono::seconds(1);
    d.inRetries++;
    std::shared_ptr<oboe::AudioStream> fresh = openStream(oboe::Direction::Input, d.inDevice, d.channels, d.rate, "input");
    if (!fresh) return false;
    d.in = fresh;
    d.inBroken = false;
    d.inXruns = 0;          // a new stream counts from zero
    text_color_set(DW_COLOR_INFO);
    dw_printf("Audio input back after %d attempt(s).\n", d.inRetries);
    return true;
}

static bool recoverOutput(Device &d, const char *why)
{
    const auto now = std::chrono::steady_clock::now();
    if (!d.outBroken) {
        d.outBroken = true;
        d.outNextRetry = now;
        text_color_set(DW_COLOR_ERROR);
        dw_printf("Audio output error: %s. Reopening the output.\n", why);
        if (d.out) { d.out->requestStop(); d.out->close(); d.out.reset(); }
    }
    if (now < d.outNextRetry) return false;
    d.outNextRetry = now + std::chrono::seconds(1);
    std::shared_ptr<oboe::AudioStream> fresh = openStream(oboe::Direction::Output, d.outDevice, d.channels, d.rate, "output");
    if (!fresh) return false;
    d.out = fresh;
    d.outBroken = false;
    text_color_set(DW_COLOR_INFO);
    dw_printf("Audio output back.\n");
    return true;
}

// Audio the input had to drop because it was not read in time, said in the
// modem log: lost audio is lost frames, and nothing else would show it.
static void reportOverruns(Device &d)
{
    const auto now = std::chrono::steady_clock::now();
    if (now < d.inXrunCheck) return;
    d.inXrunCheck = now + XrunCheckPeriod;
    if (!d.in->isXRunCountSupported()) return;
    const oboe::ResultWithValue<int32_t> xruns = d.in->getXRunCount();
    if (!xruns || xruns.value() <= d.inXruns) return;
    text_color_set(DW_COLOR_ERROR);
    dw_printf("Audio input overrun: %d more, %d since the input opened. The receiver lost audio and may have missed frames.\n",
              xruns.value() - d.inXruns, xruns.value());
    d.inXruns = xruns.value();
}

// One octet of input.  Blocks until a block arrives; while the stream is
// down, a block of silence at the stream's own pace, never an end of
// stream: Dire Wolf's receiver treats one of those as fatal.
extern "C" int audio_get(int a)
{
    Device &d = g_dev[a];
    if (d.inPos >= d.inLen) {
        const int frames = BlockBytes / d.bytesPerFrame;
        bool haveData = false;
        if (d.in && !d.inBroken) {
            const oboe::ResultWithValue<int32_t> result = d.in->read(d.inbuf, frames, ReadTimeoutNs);
            if (!result) {
                recoverInput(d, oboe::convertToText(result.error()));
            } else if (result.value() > 0) {
                d.inLen = result.value() * d.bytesPerFrame;
                haveData = true;
                reportOverruns(d);
            }
        } else {
            recoverInput(d, "stream closed");
        }
        if (!haveData) {
            std::memset(d.inbuf, 0, sizeof(d.inbuf));
            d.inLen = BlockBytes;
            std::this_thread::sleep_for(blockDuration(d));
        }
        d.inPos = 0;
    }
    return d.inbuf[d.inPos++];
}

extern "C" int audio_put(int a, int c)
{
    Device &d = g_dev[a];
    d.outbuf[d.outLen++] = static_cast<unsigned char>(c);
    if (d.outLen >= BlockBytes) return audio_flush(a);
    return 0;
}

extern "C" int audio_flush(int a)
{
    Device &d = g_dev[a];
    // Pad an incomplete last frame with silence.
    while (d.outLen % d.bytesPerFrame) d.outbuf[d.outLen++] = 0;
    if (!d.out || d.outBroken) {
        if (!recoverOutput(d, "stream closed")) {
            // Nowhere to play it: the block is lost, the transmitter not.
            d.outLen = 0;
            return -1;
        }
    }
    int offset = 0;
    while (offset < d.outLen) {
        const int frames = (d.outLen - offset) / d.bytesPerFrame;
        const oboe::ResultWithValue<int32_t> result = d.out->write(d.outbuf + offset, frames, WriteTimeoutNs);
        if (!result) {
            recoverOutput(d, oboe::convertToText(result.error()));
            d.outLen = 0;
            return -1;
        }
        offset += result.value() * d.bytesPerFrame;
        d.framesWritten += result.value();
    }
    d.outLen = 0;
    return 0;
}

// Wait until the last sample written has been played, before the PTT is
// released.  Dire Wolf releases it as soon as audio_wait() returns and the
// frame's own duration has elapsed since keying - but on a phone the sound
// comes out a whole output latency after it is written (the mixer, the HAL,
// a USB sound card: 40 to well over 100 ms).  Returning once the buffer was
// handed over, as before, cut the end of every frame - the FCS and the
// closing flag - whenever that latency exceeded TXTAIL: frames sent from the
// phone, the ACKs among them, were lost.
//
// Oboe's calculateLatencyMillis() is how long a frame written now takes to
// be heard: waiting that long after the last write covers what is buffered
// and what lies beyond.  Without it (OpenSL ES, or a stream that cannot say
// yet), the buffer's duration plus a margin for the rest of the path.
extern "C" void audio_wait(int a)
{
    Device &d = g_dev[a];
    audio_flush(a);
    if (!d.out || d.outBroken) return;
    int ms = -1;
    const oboe::ResultWithValue<double> latency = d.out->calculateLatencyMillis();
    if (latency && latency.value() > 0) ms = static_cast<int>(latency.value() + 0.5) + 10;
    if (ms < 0) {
        const int32_t buffered = d.out->getBufferSizeInFrames();
        ms = static_cast<int>(1000LL * buffered / d.rate) + 60;
    }
    ms = std::min(ms, 1000);       // a nonsense estimate must not hold the PTT down
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

extern "C" int audio_close(void)
{
    for (int a = 0; a < MAX_ADEVS; a++) {
        Device &d = g_dev[a];
        if (d.in) { d.in->requestStop(); d.in->close(); d.in.reset(); }
        if (d.out) { d.out->requestStop(); d.out->close(); d.out.reset(); }
    }
    return 0;
}
