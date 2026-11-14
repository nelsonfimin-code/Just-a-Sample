#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "InstrumentModel.h"

namespace sampler
{
struct TransientMarker
{
    std::uint64_t frame = 0;
    float strength = 0.0f;
};

struct TransientAnalysisOptions
{
    float sensitivity = 0.5f;
    std::uint32_t windowMs = 5;
    std::uint32_t minimumIntervalMs = 80;
    std::size_t maximumMarkers = 2048;
};

struct TransientAnalysisResult
{
    std::vector<TransientMarker> markers;
    float threshold = 0.0f;
    float peakLevel = 0.0f;
};

struct SliceGenerationOptions
{
    std::uint8_t firstKey = 36;
    std::uint8_t lastKey = 84;
    std::uint8_t velocityLow = 1;
    std::uint8_t velocityHigh = 127;
    std::uint64_t minimumSliceFrames = 64;
    std::size_t maximumSlices = 128;
};

// Converts transient markers into one-key zones. sampleEnd is exclusive.
bool buildSliceZones(const TransientAnalysisResult& analysis,
                     const SampleReference& sample,
                     const SliceGenerationOptions& options,
                     std::vector<SampleZone>& zones,
                     std::string* error = nullptr);

bool analyzeTransients(const float* const* channels,
                      std::size_t channelCount,
                      std::uint64_t frameCount,
                      double sampleRate,
                      const TransientAnalysisOptions& options,
                      TransientAnalysisResult& result,
                      std::string* error = nullptr);
}
