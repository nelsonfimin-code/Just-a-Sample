#include "SampleAnalysis.h"

#include <algorithm>
#include <cmath>

namespace sampler
{
bool analyzeTransients(const float* const* channels,
                       std::size_t channelCount,
                       std::uint64_t frameCount,
                       double sampleRate,
                       const TransientAnalysisOptions& options,
                       TransientAnalysisResult& result,
                       std::string* error)
{
    result = {};
    if (error)
        error->clear();

    const auto fail = [error](const char* message)
    {
        if (error)
            *error = message;
        return false;
    };

    if (!channels || channelCount == 0 || channelCount > 8 || frameCount < 3)
        return fail("Transient analysis requires 1 to 8 channels and at least 3 frames.");

    if (!std::isfinite(sampleRate) || sampleRate < 1000.0 || sampleRate > 768000.0)
        return fail("Invalid sample rate.");

    if (!std::isfinite(options.sensitivity) || options.sensitivity < 0.0f || options.sensitivity > 1.0f)
        return fail("Sensitivity must be between 0 and 1.");

    if (options.windowMs < 1 || options.windowMs > 100
        || options.minimumIntervalMs < 5 || options.minimumIntervalMs > 2000
        || options.maximumMarkers < 2 || options.maximumMarkers > 4096)
        return fail("Transient analysis settings are outside supported bounds.");

    for (std::size_t channel = 0; channel < channelCount; ++channel)
        if (!channels[channel])
            return fail("A sample channel pointer is null.");

    const auto windowFrames = std::max<std::uint64_t>(
        1, std::uint64_t(std::llround(sampleRate * double(options.windowMs) / 1000.0)));
    const auto hopFrames = std::max<std::uint64_t>(1, windowFrames / 2);
    const auto blockCount = std::size_t((frameCount + hopFrames - 1) / hopFrames);
    if (blockCount > 16u * 1024u * 1024u)
        return fail("Sample is too long for the configured transient analysis window.");

    std::vector<float> envelope(blockCount, 0.0f);
    float peak = 0.0f;

    for (std::size_t block = 0; block < blockCount; ++block)
    {
        const auto start = std::uint64_t(block) * hopFrames;
        const auto end = std::min(frameCount, start + windowFrames);
        double sumSquares = 0.0;
        std::uint64_t count = 0;

        for (std::uint64_t frame = start; frame < end; ++frame)
        {
            for (std::size_t channel = 0; channel < channelCount; ++channel)
            {
                const auto value = channels[channel][frame];
                if (!std::isfinite(value))
                    return fail("Sample contains a non-finite audio value.");
                sumSquares += double(value) * double(value);
                ++count;
            }
        }

        const auto rms = count ? float(std::sqrt(sumSquares / double(count))) : 0.0f;
        envelope[block] = rms;
        peak = std::max(peak, rms);
    }

    result.markers.clear();
    result.peakLevel = peak;
    result.threshold = std::max(1.0e-7f, peak * (0.02f + (1.0f - options.sensitivity) * 0.48f));
    result.markers.push_back({ 0, peak > 0.0f ? envelope.front() / peak : 0.0f });

    if (peak <= 1.0e-7f)
        return true;

    const auto minimumGapFrames = std::uint64_t(
        std::llround(sampleRate * double(options.minimumIntervalMs) / 1000.0));

    for (std::size_t block = 1; block + 1 < envelope.size(); ++block)
    {
        const auto level = envelope[block];
        if (level < result.threshold || level < envelope[block - 1] || level <= envelope[block + 1])
            continue;

        const auto frame = std::min(frameCount - 1, std::uint64_t(block) * hopFrames);
        if (result.markers.size() > 1
            && frame - result.markers.back().frame < minimumGapFrames)
        {
            if (level > result.markers.back().strength * peak)
            {
                result.markers.back().frame = frame;
                result.markers.back().strength = level / peak;
            }
            continue;
        }

        result.markers.push_back({ frame, std::clamp(level / peak, 0.0f, 1.0f) });
        if (result.markers.size() >= options.maximumMarkers)
            break;
    }

    return true;
}
bool buildSliceZones(const TransientAnalysisResult& analysis,
                     const SampleReference& sample,
                     const SliceGenerationOptions& options,
                     std::vector<SampleZone>& zones,
                     std::string* error)
{
    const auto fail = [error](const char* message)
    {
        if (error)
            *error = message;
        return false;
    };

    if (sample.id.empty() || sample.path.empty() || sample.frames == 0
        || !std::isfinite(sample.sampleRate) || sample.sampleRate < 1000.0
        || sample.channels == 0 || sample.channels > 8)
        return fail("Slice generation requires a valid sample reference.");
    if (options.firstKey > options.lastKey || options.velocityLow == 0
        || options.velocityLow > options.velocityHigh || options.maximumSlices == 0
        || options.maximumSlices > 128 || options.minimumSliceFrames == 0)
        return fail("Slice generation settings are outside supported bounds.");
    if (analysis.markers.empty())
        return fail("No transient markers are available for slicing.");

    const auto keyCapacity = std::size_t(options.lastKey - options.firstKey) + 1;
    const auto capacity = std::min(keyCapacity, options.maximumSlices);
    std::vector<std::uint64_t> boundaries;
    boundaries.reserve(analysis.markers.size() + 2);
    boundaries.push_back(0);
    for (const auto& marker : analysis.markers)
        if (marker.frame > 0 && marker.frame < sample.frames)
            boundaries.push_back(marker.frame);
    boundaries.push_back(sample.frames);
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    std::vector<SampleZone> generated;
    generated.reserve(capacity);
    for (std::size_t i = 0; i + 1 < boundaries.size() && generated.size() < capacity; ++i)
    {
        const auto start = boundaries[i];
        const auto end = boundaries[i + 1];
        if (end <= start || end - start < options.minimumSliceFrames)
            continue;

        const auto key = std::uint8_t(std::size_t(options.firstKey) + generated.size());
        SampleZone zone;
        zone.id = sample.id + "-slice-" + std::to_string(generated.size() + 1);
        zone.sample = sample;
        zone.keyLow = key;
        zone.keyHigh = key;
        zone.rootKey = key;
        zone.velocityLow = options.velocityLow;
        zone.velocityHigh = options.velocityHigh;
        zone.sampleStart = start;
        zone.sampleEnd = end;
        generated.push_back(std::move(zone));
    }

    if (generated.empty())
        return fail("No slices met the minimum slice length.");

    zones = std::move(generated);
    return true;
}

}
