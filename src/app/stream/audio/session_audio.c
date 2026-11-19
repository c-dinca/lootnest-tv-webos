#include "lootnest.h"
#include <stdlib.h>
#include <assert.h>

#include <opus_multistream.h>

#include "ss4s.h"
#include "stream/connection/session_connection.h"
#include "stream/session_priv.h"
#include "logging.h"

#define SAMPLES_PER_FRAME  240

static session_t *session = NULL;
static SS4S_Player *player = NULL;
static OpusMSDecoder *decoder = NULL;
static unsigned char *buffer = NULL;
static int frame_size = 0, unit_size = 0;

// Lootnest: sound kept in time. Packets are fed as they arrive (direct
// submit), so nothing upstream ever drops any: a late burst after a gap,
// which the TV fills with silence, stays as lag for the rest of the stream.
// The sound ahead of the clock is fed minus elapsed; its lowest point is
// where the TV last ran dry, so what the TV holds is about the lead over
// that low. Past LAG_MAX_US, one packet in LAG_SKIP_EVERY is left out
// until it's back under, a few milliseconds at a time.
#define LAG_MAX_US 60000
#define LAG_SKIP_EVERY 4
static Uint64 lag_t0 = 0, lag_fed_us = 0, lag_packet_us = 5000;
static Sint64 lag_low_us = 0;
static unsigned lag_count = 0, lag_skipped = 0;

AUDIO_INFO audio_stream_info;

static size_t opus_head_serialize(const OPUS_MULTISTREAM_CONFIGURATION *config, unsigned char *data);

static void aud_buffers_free();

static int aud_init(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void *context,
                    int arFlags) {
    (void) audioConfiguration;
    (void) arFlags;
    memset(&audio_stream_info, 0, sizeof(audio_stream_info));
    lag_t0 = 0;
    lag_fed_us = 0;
    lag_low_us = 0;
    lag_count = 0;
    lag_skipped = 0;
    if (opusConfig->sampleRate > 0 && opusConfig->samplesPerFrame > 0) {
        lag_packet_us = (Uint64) opusConfig->samplesPerFrame * 1000000 / (Uint64) opusConfig->sampleRate;
    }
    session = context;
    player = session->player;
    SS4S_AudioCodec codec = SS4S_AUDIO_PCM_S16LE;
    size_t codecDataLen = 0;
    SS4S_AudioInfo info = {
            .numOfChannels = opusConfig->channelCount,
            .appName = "Lootnest",
            .streamName = "Streaming",
            .sampleRate = opusConfig->sampleRate,
            .samplesPerFrame = SAMPLES_PER_FRAME,
    };
    if (session->audio_cap.codecs & SS4S_AUDIO_OPUS && SS4S_GetAudioPreferredCodecs(&info) & SS4S_AUDIO_OPUS) {
        codec = SS4S_AUDIO_OPUS;
        decoder = NULL;
        buffer = calloc(1024, sizeof(unsigned char));
        if (buffer == NULL) {
            commons_log_error("Session", "Audio init: failed to allocate codec data buffer");
            return -1;
        }
        codecDataLen = opus_head_serialize(opusConfig, buffer);
    } else {
        int rc;
        decoder = opus_multistream_decoder_create(opusConfig->sampleRate, opusConfig->channelCount, opusConfig->streams,
                                                  opusConfig->coupledStreams, opusConfig->mapping, &rc);
        if (rc != 0) {
            return rc;
        }
        frame_size = opusConfig->samplesPerFrame;
        unit_size = (int) (opusConfig->channelCount * sizeof(int16_t));
        buffer = calloc(unit_size, frame_size);
        if (buffer == NULL) {
            commons_log_error("Session", "Audio init: failed to allocate decode buffer");
            aud_buffers_free();
            return -1;
        }
    }
    audio_stream_info.format = SS4S_AudioCodecName(codec);
    switch (audioConfiguration) {
        case AUDIO_CONFIGURATION_STEREO:
            audio_stream_info.channels = "Stereo";
            break;
        case AUDIO_CONFIGURATION_51_SURROUND:
            audio_stream_info.channels = "5.1ch";
            break;
        case AUDIO_CONFIGURATION_71_SURROUND:
            audio_stream_info.channels = "7.1ch";
            break;
        default:
            audio_stream_info.channels = "Unknown";
            break;
    }
    info.codec = codec;
    info.codecData = buffer;
    info.codecDataLen = codecDataLen;
    // aud_cleanup only runs once the audio stream has started, so a failed init has to release
    // what it allocated itself.
    SS4S_AudioOpenResult result = SS4S_PlayerAudioOpen(player, &info);
    lootnest_note("Sound: %s, %d channels at %d Hz, decoded %s: %s", SS4S_AudioCodecName(codec), opusConfig->channelCount,
                  opusConfig->sampleRate, codec == SS4S_AUDIO_OPUS ? "by the TV" : "by the app",
                  result == SS4S_AUDIO_OPEN_OK ? "opened" : "the TV didn't open it");
    if (result != SS4S_AUDIO_OPEN_OK) {
        aud_buffers_free();
    }
    return result;
}

static void aud_cleanup() {
    if (lag_skipped > 0) {
        lootnest_note("Sound: %u ms left out to keep it in time with the picture",
                      (unsigned) (lag_skipped * lag_packet_us / 1000));
    }
    if (player != NULL) {
        SS4S_PlayerAudioClose(player);
        player = NULL;
    }
    aud_buffers_free();
    session = NULL;
}

static void aud_buffers_free() {
    if (decoder != NULL) {
        opus_multistream_decoder_destroy(decoder);
        decoder = NULL;
    }
    free(buffer);
    buffer = NULL;
}

// in_time says whether this packet goes to the TV, and counts it.
static bool in_time(void) {
    Uint64 now = SDL_GetPerformanceCounter(), freq = SDL_GetPerformanceFrequency();
    if (lag_t0 == 0) {
        lag_t0 = now;
    }
    Uint64 elapsed_us = (now - lag_t0) * 1000000 / freq;
    Sint64 lead_us = (Sint64) lag_fed_us - (Sint64) elapsed_us;
    if (lead_us < lag_low_us) {
        lag_low_us = lead_us;
    }
    lag_count++;
    if (lead_us - lag_low_us > LAG_MAX_US && lag_count % LAG_SKIP_EVERY == 0) {
        lag_skipped++;
        return false;
    }
    lag_fed_us += lag_packet_us;
    return true;
}

static void aud_feed(char *sampleData, int sampleLength) {
    // A packet left out is still decoded, so the decoder's state runs on.
    bool keep = in_time();
    if (decoder != NULL) {
        int decode_len = opus_multistream_decode(decoder, (unsigned char *) sampleData, sampleLength,
                                                 (opus_int16 *) buffer, frame_size, 0);
        if (keep) {
            SS4S_PlayerAudioFeed(player, buffer, unit_size * decode_len);
        }
    } else if (keep) {
        SS4S_PlayerAudioFeed(player, (unsigned char *) sampleData, sampleLength);
    }
}

static size_t opus_head_serialize(const OPUS_MULTISTREAM_CONFIGURATION *config, unsigned char *data) {
    unsigned char *ptr = data;
    // 1. Magic Signature:
    ptr = memcpy(ptr, "OpusHead", 8) + 8;
    // 2. Version (8 bits, unsigned):
    *ptr++ = 1;
    // 3. Output Channel Count 'C' (8 bits, unsigned):
    *ptr++ = config->channelCount;
    // 4. Pre-skip (16 bits, unsigned, little-endian):
    uint16_t preskip = SDL_SwapLE16(0);
    ptr = memcpy(ptr, &preskip, 2) + 2;
    // 5. Input Sample Rate (32 bits, unsigned, little-endian):
    uint32_t sampleRate = SDL_SwapLE32(config->sampleRate);
    ptr = memcpy(ptr, &sampleRate, 4) + 4;
    // 6. Output Gain (16 bits, signed, little-endian):
    int16_t outputGain = SDL_SwapLE16(0);
    ptr = memcpy(ptr, &outputGain, 2) + 2;
    // 7. Channel Mapping Family (8 bits, unsigned):
    *ptr++ = 1;
    // 8.1. Stream Count 'N' (8 bits, unsigned):
    *ptr++ = config->streams;
    // 8.2. Coupled Stream Count 'M' (8 bits, unsigned):
    *ptr++ = config->coupledStreams;
    // 8.3. Channel Mapping (8*C bits):
    ptr = memcpy(ptr, config->mapping, config->channelCount) + config->channelCount;
    return ptr - data;
}

AUDIO_RENDERER_CALLBACKS ss4s_aud_callbacks = {
        .init = aud_init,
        .cleanup = aud_cleanup,
        .decodeAndPlaySample = aud_feed,
        .capabilities = CAPABILITY_DIRECT_SUBMIT,
};