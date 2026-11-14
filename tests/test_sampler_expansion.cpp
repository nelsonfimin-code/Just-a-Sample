#include "../Source/Expansion/InstrumentModel.h"
#include "../Source/Expansion/MPEExpression.h"
#include "../Source/Expansion/ModulationMatrix.h"
#include "../Source/Expansion/MultisampleEngine.h"
#include "../Source/Expansion/SampleAnalysis.h"
#include "../Source/Expansion/InstrumentFileIO.h"

#include <cassert>
#include <chrono>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <cmath>
#include <iostream>
#include <string>
#include <utility>

using namespace sampler;

namespace
{
std::string makeTemporaryDirectory()
{
#if defined(_WIN32)
    char temporaryPath[MAX_PATH] {};
    char temporaryName[MAX_PATH] {};
    if (GetTempPathA(MAX_PATH, temporaryPath) == 0
        || GetTempFileNameA(temporaryPath, "jas", 0, temporaryName) == 0)
        return {};
    if (!DeleteFileA(temporaryName) || !CreateDirectoryA(temporaryName, nullptr))
        return {};
    return temporaryName;
#else
    char temporaryPath[] = "/tmp/jasz-io-test-XXXXXX";
    const auto* created = mkdtemp(temporaryPath);
    return created != nullptr ? std::string(created) : std::string{};
#endif
}

bool createTestDirectory(const std::string& path)
{
#if defined(_WIN32)
    return _mkdir(path.c_str()) == 0;
#else
    return mkdir(path.c_str(), 0700) == 0;
#endif
}

void removeTestDirectory(const std::string& path)
{
#if defined(_WIN32)
    _rmdir(path.c_str());
#else
    rmdir(path.c_str());
#endif
}

SampleZone makeZone(const std::string& id, int keyLow, int keyHigh,
                    int velocityLow, int velocityHigh, int roundRobin = 0)
{
    SampleZone zone;
    zone.id = id;
    zone.sample = { "sample-" + id, "/samples/" + id + ".wav", 48000.0, 2, 48000 };
    zone.keyLow = std::uint8_t(keyLow);
    zone.keyHigh = std::uint8_t(keyHigh);
    zone.velocityLow = std::uint8_t(velocityLow);
    zone.velocityHigh = std::uint8_t(velocityHigh);
    zone.rootKey = std::uint8_t((keyLow + keyHigh) / 2);
    zone.roundRobinGroup = std::uint8_t(roundRobin);
    return zone;
}

void testInstrumentModel()
{
    InstrumentModel model;
    assert(model.addZone(makeZone("soft", 48, 72, 1, 63)));
    assert(model.addZone(makeZone("hard", 48, 72, 64, 127)));

    const auto soft = model.select(60, 40);
    const auto hard = model.select(60, 100);
    assert(soft.size() == 1 && soft[0].zone->id == "soft");
    assert(hard.size() == 1 && hard[0].zone->id == "hard");
    assert(!model.addZone(makeZone("bad", 72, 48, 1, 127)));
    assert(model.validate());

    auto loopZone = makeZone("loop-map", 60, 60, 1, 127);
    loopZone.sampleStart = 100;
    loopZone.sampleEnd = 40000;
    loopZone.loopStart = 1000;
    loopZone.loopEnd = 12000;
    loopZone.loopCrossfadeFrames = 100;
    loopZone.loopMode = ZoneLoopMode::Forward;
    assert(model.addZone(loopZone));

    const auto preset = model.serialize();
    assert(!preset.empty());
    InstrumentModel restored;
    assert(restored.deserialize(preset));
    assert(restored.size() == model.size());
    assert(restored.zones()[0].id == "soft" && restored.zones()[1].velocityLow == 64);
    assert(restored.zones()[2].sampleStart == 100);
    assert(restored.zones()[2].loopEnd == 12000);
    assert(restored.zones()[2].loopMode == ZoneLoopMode::Forward);

    auto corrupt = preset;
    corrupt[0] = 'X';
    assert(!restored.deserialize(corrupt) && restored.size() == model.size());
    auto truncated = preset;
    truncated.pop_back();
    assert(!restored.deserialize(truncated) && restored.size() == model.size());

    auto invalidLoop = makeZone("invalid-loop", 60, 60, 1, 127);
    invalidLoop.loopMode = ZoneLoopMode::Forward;
    invalidLoop.loopStart = 500;
    invalidLoop.loopEnd = 400;
    InstrumentModel invalid;
    assert(!invalid.addZone(invalidLoop));

    InstrumentModel crowded;
    for (int i = 0; i < 129; ++i)
        assert(crowded.addZone(makeZone("layer-" + std::to_string(i), 0, 127, 1, 127)));
    const auto crowdedSelection = crowded.select(60, 90);
    assert(crowdedSelection.size() == 128 && crowdedSelection.overflowed());

    InstrumentModel roundRobin;
    assert(roundRobin.addZone(makeZone("always", 0, 127, 1, 127)));
    assert(roundRobin.addZone(makeZone("rr-a", 0, 127, 1, 127, 1)));
    assert(roundRobin.addZone(makeZone("rr-b", 0, 127, 1, 127, 1)));
    const auto first = roundRobin.select(60, 100);
    const auto second = roundRobin.select(60, 100);
    assert(first.size() == 2 && second.size() == 2);
    assert(first[1].zone->id != second[1].zone->id);
}

void testMPE()
{
    MPEExpression mpe({ MPEZoneConfig::Layout::Lower, 15, 48.0f });
    assert(mpe.isMemberChannel(2) && !mpe.isMemberChannel(1));
    mpe.process({ MPEEvent::Type::PitchBend, 2, 60, 0, 16383 });
    mpe.process({ MPEEvent::Type::ChannelPressure, 2, 0, 0, 100 });
    mpe.process({ MPEEvent::Type::ControlChange, 2, 0, 74, 80 });
    mpe.process({ MPEEvent::Type::PolyPressure, 2, 60, 0, 64 });
    const auto expression = mpe.forChannel(2);
    assert(expression.pitchBendSemitones > 47.9f);
    assert(std::abs(expression.pressure - 100.0f / 127.0f) < 0.0001f);
    assert(std::abs(expression.slide - 80.0f / 127.0f) < 0.0001f);
    assert(std::abs(mpe.perNotePressure(2, 60) - 64.0f / 127.0f) < 0.0001f);
    mpe.process({ MPEEvent::Type::ControlChange, 2, 0, 64, 0 });
    assert(mpe.forChannel(2).sustain == 0.0f);
    assert(mpe.perNotePressure(2, 200) == 0.0f);

    mpe.process({ MPEEvent::Type::PitchBend, 1, 0, 0, 12288 });
    mpe.process({ MPEEvent::Type::ChannelPressure, 1, 0, 0, 90 });
    mpe.process({ MPEEvent::Type::ControlChange, 1, 0, 64, 127 });
    const auto combined = mpe.forChannel(2);
    assert(combined.pitchBendSemitones > 71.9f && combined.pitchBendSemitones < 72.1f);
    assert(combined.pressure >= 90.0f / 127.0f && combined.sustain == 1.0f);

    mpe.process({ MPEEvent::Type::ControlChange, 1, 0, 1, 127 });
    const auto inputs = mpe.modulationInputs(2, 60, 0.8f, 0.25f, 0.5f, 0.7f);
    assert(inputs.velocity == 0.8f && inputs.modWheel == 1.0f);
    assert(inputs.slide > 0.62f && inputs.lfo1 == 0.5f);

    MPEExpression upper({ MPEZoneConfig::Layout::Upper, 4, 48.0f });
    assert(upper.isMemberChannel(15));
    assert(!upper.isMemberChannel(16) && !upper.isMemberChannel(11));
    mpe.reset();
    assert(mpe.forChannel(2).pressure == 0.0f);
}

void testModulation()
{
    ModulationMatrix matrix;
    ModulationInputs inputs;
    inputs.velocity = 0.75f;
    inputs.slide = 0.5f;

    assert(matrix.addRoute({ ModSource::Velocity, ModDestination::GainDb, 12.0f, true }));
    assert(matrix.addRoute({ ModSource::Slide, ModDestination::Pan, 0.8f, true }));
    assert(std::abs(matrix.evaluate(ModDestination::GainDb, -6.0f, -24.0f, 12.0f, inputs) - 3.0f) < 0.0001f);
    assert(std::abs(matrix.evaluate(ModDestination::Pan, 0.0f, -1.0f, 1.0f, inputs) - 0.4f) < 0.0001f);

    auto disabled = *matrix.route(0);
    disabled.enabled = false;
    assert(matrix.setRoute(0, disabled));
    assert(matrix.evaluate(ModDestination::GainDb, -6.0f, -24.0f, 12.0f, inputs) == -6.0f);
    assert(!matrix.setRoute(99, disabled));
    assert(matrix.removeRoute(0) && matrix.size() == 1);

    Lfo lfo;
    lfo.configure(2.0f, Lfo::Shape::Square);
    assert(lfo.next(48000.0f) == 1.0f);
    lfo.reset(0.75f);
    assert(lfo.next(48000.0f) == -1.0f);
}


void testSampleAnalysis()
{
    constexpr std::size_t frameCount = 8192;
    float left[frameCount] = {};
    float right[frameCount] = {};
    left[100] = 1.0f;
    left[1100] = 0.8f;
    left[3000] = 0.9f;
    left[6000] = 0.7f;
    const float* channels[] = { left, right };

    TransientAnalysisOptions options;
    options.sensitivity = 0.5f;
    options.windowMs = 1;
    options.minimumIntervalMs = 500;
    TransientAnalysisResult result;
    std::string error;
    assert(analyzeTransients(channels, 2, frameCount, 1000.0, options, result, &error));
    assert(result.markers.size() == 5);
    assert(result.markers[0].frame == 0);
    assert(result.markers[1].frame == 100);
    assert(result.markers[2].frame == 1100);
    assert(result.markers[3].frame == 3000);
    assert(result.markers[4].frame == 6000);
    assert(result.peakLevel > 0.0f && result.threshold > 0.0f);


    SampleReference sample;
    sample.id = "drum-loop";
    sample.path = "/samples/drum-loop.wav";
    sample.sampleRate = 48000.0;
    sample.channels = 2;
    sample.frames = frameCount;
    SliceGenerationOptions sliceOptions;
    sliceOptions.firstKey = 48;
    sliceOptions.lastKey = 60;
    sliceOptions.minimumSliceFrames = 128;
    std::vector<SampleZone> slices;
    assert(buildSliceZones(result, sample, sliceOptions, slices, &error));
    assert(slices.size() == 4);
    assert(slices.front().sampleStart == 100 && slices.front().sampleEnd == 1100);
    assert(slices.back().sampleEnd == frameCount);
    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        assert(slices[i].keyLow == 48 + i && slices[i].keyHigh == 48 + i);
        assert(slices[i].rootKey == slices[i].keyLow);
        assert(slices[i].sampleEnd > slices[i].sampleStart);
    }
    InstrumentModel slicedInstrument;
    for (const auto& slice : slices)
        assert(slicedInstrument.addZone(slice));
    assert(slicedInstrument.size() == slices.size());
    const auto serializedSlices = slicedInstrument.serialize(&error);
    InstrumentModel restoredSlices;
    assert(restoredSlices.deserialize(serializedSlices, &error));
    assert(restoredSlices.size() == slices.size());
    assert(restoredSlices.zones()[0].sampleStart == 100);
    assert(restoredSlices.zones()[3].sampleEnd == frameCount);

    TransientAnalysisResult unorderedMarkers;
    unorderedMarkers.markers = { { 6000, 0.7f }, { 0, 0.0f }, { 3000, 0.9f },
                                  { 1100, 0.8f }, { 100, 1.0f } };
    std::vector<SampleZone> unorderedSlices;
    assert(buildSliceZones(unorderedMarkers, sample, sliceOptions, unorderedSlices, &error));
    assert(unorderedSlices.size() == slices.size());
    assert(unorderedSlices.front().sampleStart == 100);
    assert(unorderedSlices.back().sampleEnd == frameCount);

    TransientAnalysisResult emptyMarkers;
    auto preservedSlices = slices;
    assert(!buildSliceZones(emptyMarkers, sample, sliceOptions, preservedSlices, &error));
    assert(preservedSlices.size() == slices.size());

    const float* invalidChannels[] = { nullptr };
    assert(!analyzeTransients(invalidChannels, 1, frameCount, 48000.0, options, result, &error));
    assert(result.markers.empty());
}

void testInstrumentFileIO()
{
    const std::string root = makeTemporaryDirectory();
    assert(!root.empty());
    const auto originalFolder = root + "/original";
    const auto searchFolder = root + "/search";
    const auto duplicateFolder = root + "/duplicate";
    assert(createTestDirectory(originalFolder));
    assert(createTestDirectory(searchFolder));
    assert(createTestDirectory(duplicateFolder));

    const auto originalSample = originalFolder + "/kick.wav";
    {
        std::ofstream sample(originalSample, std::ios::binary);
        sample << "sample-placeholder";
    }

    SampleZone zone = makeZone("kick-zone", 48, 48, 1, 127);
    zone.sample.id = "kick";
    zone.sample.path = originalSample;
    zone.sample.sampleRate = 48000.0;
    zone.sample.channels = 1;
    zone.sample.frames = 1024;
    InstrumentModel model;
    zone.filterMode = ZoneFilterMode::LowPass;
    zone.filterCutoffHz = 3200.0f;
    zone.filterResonance = 0.65f;
    zone.driveDb = 6.0f;
    zone.reverbSend = 0.35f;
    zone.delaySend = 0.22f;
    assert(model.addZone(zone));

    const auto preset = root + "/KickKit.jasz";
    std::string error;
    assert(saveInstrumentFile(model, preset, &error));
    InstrumentModel loaded;
    assert(loadInstrumentFile(preset, loaded, &error));
    assert(loaded.size() == 1 && loaded.zones()[0].sample.path == originalSample);
    assert(loaded.zones()[0].filterMode == ZoneFilterMode::LowPass);
    assert(std::abs(loaded.zones()[0].filterCutoffHz - 3200.0f) < 0.001f);
    assert(std::abs(loaded.zones()[0].filterResonance - 0.65f) < 0.001f);
    assert(std::abs(loaded.zones()[0].driveDb - 6.0f) < 0.001f);
    assert(std::abs(loaded.zones()[0].reverbSend - 0.35f) < 0.001f);
    assert(std::abs(loaded.zones()[0].delaySend - 0.22f) < 0.001f);

    // v4 retained resonance but did not yet have time-effect sends.
    auto v4Bytes = model.serialize(&error);
    v4Bytes[4] = 4; v4Bytes[5] = 0; v4Bytes[6] = 0; v4Bytes[7] = 0;
    v4Bytes.resize(v4Bytes.size() - 8);
    InstrumentModel v4Loaded;
    assert(v4Loaded.deserialize(v4Bytes, &error));
    assert(std::abs(v4Loaded.zones()[0].filterResonance - 0.65f) < 0.001f);
    assert(v4Loaded.zones()[0].reverbSend == 0.0f && v4Loaded.zones()[0].delaySend == 0.0f);

    // v3 did not contain resonance or time-effect sends.
    auto v3Bytes = model.serialize(&error);
    v3Bytes[4] = 3; v3Bytes[5] = 0; v3Bytes[6] = 0; v3Bytes[7] = 0;
    v3Bytes.resize(v3Bytes.size() - 12);
    InstrumentModel v3Loaded;
    assert(v3Loaded.deserialize(v3Bytes, &error));
    assert(v3Loaded.zones()[0].filterResonance == 0.0f);
    assert(v3Loaded.zones()[0].reverbSend == 0.0f && v3Loaded.zones()[0].delaySend == 0.0f);

    // Newer zone effects fields must not break older v1/v2 instrument maps.
    auto v2Bytes = model.serialize(&error);
    assert(v2Bytes.size() > 21);
    v2Bytes[4] = 2; v2Bytes[5] = 0; v2Bytes[6] = 0; v2Bytes[7] = 0;
    v2Bytes.resize(v2Bytes.size() - 21);
    InstrumentModel v2Loaded;
    assert(v2Loaded.deserialize(v2Bytes, &error));
    assert(v2Loaded.zones()[0].filterMode == ZoneFilterMode::Off);
    assert(v2Loaded.zones()[0].filterResonance == 0.0f);

    auto v1Bytes = v2Bytes;
    v1Bytes[4] = 1;
    v1Bytes.resize(v1Bytes.size() - 37);
    InstrumentModel v1Loaded;
    assert(v1Loaded.deserialize(v1Bytes, &error));
    assert(v1Loaded.zones()[0].loopMode == ZoneLoopMode::Off);
    assert(v1Loaded.zones()[0].filterMode == ZoneFilterMode::Off);

    assert(!saveInstrumentFile(model, root + "/invalid.txt", &error));
    InstrumentModel emptyInstrument;
    assert(!saveInstrumentFile(emptyInstrument, root + "/empty.jasz", &error));
    const auto corruptPreset = root + "/Corrupt.jasz";
    {
        std::ofstream corrupt(corruptPreset, std::ios::binary);
        corrupt << "not-a-jasz-file";
    }
    InstrumentModel unchangedAfterCorruptLoad = loaded;
    assert(!loadInstrumentFile(corruptPreset, unchangedAfterCorruptLoad, &error));
    assert(unchangedAfterCorruptLoad.size() == loaded.size());

    const auto movedSample = searchFolder + "/kick.wav";
    assert(std::rename(originalSample.c_str(), movedSample.c_str()) == 0);
    auto missingModel = loaded;
    missingModel.zones()[0].sample.path = originalSample;
    InstrumentModel relinked;
    RelinkReport report;
    assert(relinkMissingSamples(missingModel, { searchFolder }, relinked, report, &error));
    assert(report.relinkedSamples == 1 && report.unresolved.empty());
    const auto& resolvedMovedSample = relinked.zones()[0].sample.path;
    std::ifstream resolvedSampleFile(resolvedMovedSample, std::ios::binary);
    assert(resolvedMovedSample.find("kick.wav") != std::string::npos && resolvedSampleFile.good());

    const auto duplicateSample = duplicateFolder + "/kick.wav";
    {
        std::ofstream sample(duplicateSample, std::ios::binary);
        sample << "another-sample";
    }
    InstrumentModel unchanged = loaded;
    assert(!relinkMissingSamples(missingModel, { searchFolder, duplicateFolder },
                                 unchanged, report, &error));
    assert(report.unresolved.size() == 1 && report.unresolved[0].candidates.size() == 2);
    assert(unchanged.zones()[0].sample.path == loaded.zones()[0].sample.path);

    std::remove(preset.c_str());
    std::remove(corruptPreset.c_str());
    std::remove(movedSample.c_str());
    std::remove(duplicateSample.c_str());
    removeTestDirectory(originalFolder);
    removeTestDirectory(searchFolder);
    removeTestDirectory(duplicateFolder);
    removeTestDirectory(root);
}

void testPlaybackEngine()
{
    InstrumentModel playbackModel;
    auto playbackZone = makeZone("playback", 60, 60, 1, 127);
    playbackZone.rootKey = 60;
    playbackZone.reverbSend = 0.4f;
    playbackZone.delaySend = 0.25f;
    assert(playbackModel.addZone(playbackZone));

    float sampleLeft[8] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f };
    float sampleRight[8] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f };
    ZoneAudio zoneAudio { &playbackModel.zones()[0], sampleLeft, sampleRight, 8, 48000.0 };

    MultisampleEngine engine;
    playbackZone.filterMode = ZoneFilterMode::LowPass;
    playbackZone.filterCutoffHz = 4000.0f;
    playbackZone.driveDb = 9.0f;
    InstrumentModel filteredPlaybackModel;
    assert(filteredPlaybackModel.addZone(playbackZone));
    ZoneAudio filteredAudio { &filteredPlaybackModel.zones()[0], sampleLeft, sampleRight, 8, 48000.0 };
    assert(engine.setInstrument(filteredPlaybackModel, &filteredAudio, 1));
    ModulationMatrix matrix;
    assert(matrix.addRoute({ ModSource::Velocity, ModDestination::GainDb, 6.0f, true }));
    assert(matrix.addRoute({ ModSource::Lfo1, ModDestination::Pan, 0.25f, true }));
    engine.setModulationMatrix(matrix);
    engine.configureLfo(5.0f, Lfo::Shape::Triangle);
    engine.setMPEZoneConfig({ MPEZoneConfig::Layout::Lower, 15, 48.0f });
    engine.setOutputSampleRate(48000.0);
    engine.handleMidi({ EngineMidiEvent::Type::NoteOn, 2, 60, 0, 127 });
    assert(engine.activeVoices() == 1);

    float outLeft[4] = {};
    float outRight[4] = {};
    float reverbSendLeft[4] = {}, reverbSendRight[4] = {};
    float delaySendLeft[4] = {}, delaySendRight[4] = {};
    engine.render(outLeft, outRight, 4, 48000.0, reverbSendLeft, reverbSendRight, delaySendLeft, delaySendRight);
    assert(outLeft[0] >= 0.0f && outLeft[3] > 0.0f && outRight[3] > 0.0f);
    assert(reverbSendLeft[0] > 0.0f && delaySendLeft[0] > 0.0f);
    engine.handleMidi({ EngineMidiEvent::Type::NoteOff, 2, 60, 0, 0 });
    assert(engine.activeVoices() == 1);
    float tailLeft[4096] = {};
    float tailRight[4096] = {};
    engine.render(tailLeft, tailRight, 4096, 48000.0);
    assert(engine.activeVoices() == 0);

    // Exercise the configured 88-voice default under a sustained render load.
    auto stressZone = makeZone("88-voice-stress", 0, 127, 1, 127);
    stressZone.rootKey = 60;
    stressZone.sample.frames = 48000;
    stressZone.loopMode = ZoneLoopMode::Forward;
    stressZone.loopStart = 0;
    stressZone.loopEnd = 48000;
    InstrumentModel stressModel;
    assert(stressModel.addZone(stressZone));
    std::vector<float> stressSamples(48000, 0.1f);
    ZoneAudio stressAudio { &stressModel.zones()[0], stressSamples.data(), stressSamples.data(),
                            stressSamples.size(), 48000.0 };
    MultisampleEngine stressEngine;
    stressEngine.setVoiceLimit(88);
    assert(stressEngine.setInstrument(stressModel, &stressAudio, 1));
    for (std::uint8_t note = 0; note < 88; ++note)
        stressEngine.handleMidi({ EngineMidiEvent::Type::NoteOn, 1, note, 0, 127 });
    assert(stressEngine.activeVoices() == 88);
    float stressLeft[512] = {}, stressRight[512] = {};
    double stressEnergy = 0.0;
    const auto stressStart = std::chrono::steady_clock::now();
    for (int block = 0; block < 60; ++block)
    {
        std::fill(std::begin(stressLeft), std::end(stressLeft), 0.0f);
        std::fill(std::begin(stressRight), std::end(stressRight), 0.0f);
        stressEngine.render(stressLeft, stressRight, 512, 48000.0);
        for (int frame = 0; frame < 512; ++frame)
        {
            assert(std::isfinite(stressLeft[frame]) && std::isfinite(stressRight[frame]));
            stressEnergy += std::abs(double(stressLeft[frame])) + std::abs(double(stressRight[frame]));
        }
    }
    const auto stressMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stressStart).count();
    assert(stressEnergy > 1.0 && stressEngine.activeVoices() == 88);
    std::cout << "88-voice stress render: 60 x 512 frames in " << stressMs << " ms\n";

    for (const auto mode : { ZoneFilterMode::LowPass, ZoneFilterMode::HighPass, ZoneFilterMode::BandPass })
    {
        auto effectZone = makeZone("effect-mode", 60, 60, 1, 127);
        effectZone.filterMode = mode;
        effectZone.filterCutoffHz = 1200.0f;
        effectZone.filterResonance = 0.85f;
        effectZone.driveDb = 12.0f;
        InstrumentModel effectModel;
        assert(effectModel.addZone(effectZone));
        ZoneAudio effectAudio { &effectModel.zones()[0], sampleLeft, sampleRight, 8, 48000.0 };
        assert(engine.setInstrument(effectModel, &effectAudio, 1));
        engine.handleMidi({ EngineMidiEvent::Type::NoteOn, 1, 60, 0, 127 });
        float effectLeft[4] = {};
        float effectRight[4] = {};
        engine.render(effectLeft, effectRight, 4, 48000.0);
        for (int i = 0; i < 4; ++i)
            assert(std::isfinite(effectLeft[i]) && std::isfinite(effectRight[i]));
        engine.reset();
    }
    engine.setModulationMatrix(ModulationMatrix {});
    engine.configureLfo(1.0f, Lfo::Shape::Sine);

    auto loopZone = makeZone("forward-loop", 60, 60, 1, 127);
    loopZone.sample.frames = 8;
    loopZone.sampleStart = 0;
    loopZone.sampleEnd = 8;
    loopZone.loopStart = 2;
    loopZone.loopEnd = 5;
    loopZone.loopMode = ZoneLoopMode::Forward;
    InstrumentModel loopModel;
    assert(loopModel.addZone(loopZone));
    ZoneAudio loopAudio { &loopModel.zones()[0], sampleLeft, sampleRight, 8, 48000.0 };
    assert(engine.setInstrument(loopModel, &loopAudio, 1));
    engine.handleMidi({ EngineMidiEvent::Type::NoteOn, 1, 60, 0, 127 });
    float loopLeft[9] = {};
    float loopRight[9] = {};
    engine.render(loopLeft, loopRight, 9, 48000.0);
    const float scale = 0.70710678f;
    const float expected[] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.3f, 0.4f, 0.5f, 0.3f };
    for (int i = 0; i < 9; ++i)
        assert(std::abs(loopLeft[i] - expected[i] * scale) < 0.0001f);

    engine.handleMidi({ EngineMidiEvent::Type::NoteOff, 1, 60, 0, 0 });
    float loopTailLeft[16] = {};
    float loopTailRight[16] = {};
    engine.render(loopTailLeft, loopTailRight, 16, 48000.0);
    assert(engine.activeVoices() == 0);

    loopZone.id = "ping-pong";
    loopZone.loopMode = ZoneLoopMode::PingPong;
    InstrumentModel pingPongModel;
    assert(pingPongModel.addZone(loopZone));
    ZoneAudio pingPongAudio { &pingPongModel.zones()[0], sampleLeft, sampleRight, 8, 48000.0 };
    assert(engine.setInstrument(pingPongModel, &pingPongAudio, 1));
    engine.handleMidi({ EngineMidiEvent::Type::NoteOn, 1, 60, 0, 127 });
    float pingPongLeft[11] = {};
    float pingPongRight[11] = {};
    engine.render(pingPongLeft, pingPongRight, 11, 48000.0);
    const float pingPongExpected[] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.4f, 0.3f, 0.4f, 0.5f, 0.4f, 0.3f };
    for (int i = 0; i < 11; ++i)
        assert(std::abs(pingPongLeft[i] - pingPongExpected[i] * scale) < 0.0001f);

    engine.handleMidi({ EngineMidiEvent::Type::NoteOff, 1, 60, 0, 0 });
    float pingPongTailLeft[16] = {};
    float pingPongTailRight[16] = {};
    engine.render(pingPongTailLeft, pingPongTailRight, 16, 48000.0);
    assert(engine.activeVoices() == 0);

    InstrumentModel invalidModel;
    assert(!engine.setInstrument(invalidModel, &zoneAudio, 1));
}
}

int main()
{
    testInstrumentModel();
    testMPE();
    testModulation();
    testSampleAnalysis();
    testInstrumentFileIO();
    testPlaybackEngine();
    std::cout << "Sampler Expansion tests passed\n";
}
