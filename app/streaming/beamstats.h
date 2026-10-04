#pragma once

// Beam: performance stats overlay with three levels of detail, drawn as a
// styled panel in place of Moonlight's plain-text stats.
//
//   Basic     FPS, frametime, total latency
//   Standard  + latency breakdown, stream format, bitrate, drops
//   Advanced  + frametime spread/jitter, per-stage FPS, network detail,
//               display and hardware info
//
// The decoder publishes a snapshot about once a second; changing the level
// re-renders the last snapshot immediately.

#include <stdint.h>

namespace BeamStats {

enum Level {
    LevelBasic = 1,
    LevelStandard,
    LevelAdvanced,
};

struct Snapshot {
    float scale;

    // Frames
    int targetFps;
    double hostFps, receivedFps, decodedFps, renderedFps;
    bool hasFrametime;
    double frametimeMs, frametimeMinMs, frametimeMaxMs, frametimeJitterMs;

    // Latency (ms)
    bool hasHostLatency;
    double hostMs, hostMinMs, hostMaxMs;
    bool hasRtt;
    double rttMs, rttVarianceMs;
    double decodeMs, queueMs, renderMs;

    // Network
    double bitrateMbps, peakBitrateMbps;
    double networkDropPct, jitterDropPct;

    // Video and display
    int width, height;
    char codec[32];
    int displayWidth, displayHeight, displayHz;
    bool vsync;

    // Hardware
    char renderer[64];
    double clientCpuPct;
};

// Current level (from BEAM_STATS_LEVEL, default Standard)
int level();
void setLevel(int level);
const char* levelName(int level);

// Clears the last snapshot at the start of a session
void reset();

// Renders the snapshot at the current level and hands it to the overlay
void publish(const Snapshot& snapshot);

// Re-renders the last snapshot (after a level change)
void refresh();

// Client GPU name, reported by the decoder backend (e.g. VAAPI vendor string)
void setGpuName(const char* vendorString);

// Moonlight's own CPU use since the previous call, as % of the whole machine
double sampleProcessCpu();

}
