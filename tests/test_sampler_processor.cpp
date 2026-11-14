#include "../Source/PluginProcessor.h"
#include "../Source/Expansion/InstrumentModel.h"
#include "../Source/Expansion/InstrumentFileIO.h"
#include "../Source/PluginParameters.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    const juce::File sampleFile(argv[1]);
    if (!sampleFile.existsAsFile()) return 3;

    sampler::InstrumentModel model;
    sampler::SampleZone zone;
    zone.id = "integration-sine";
    zone.sample.id = "integration-sine-sample";
    zone.sample.path = sampleFile.getFullPathName().toStdString();
    zone.sample.sampleRate = 48000.0;
    zone.sample.channels = 2;
    zone.sample.frames = 24000;
    zone.sampleEnd = 5000;
    zone.reverbSend = 0.35f;
    zone.delaySend = 0.25f;
    zone.rootKey = 60;
    zone.keyLow = 60;
    zone.keyHigh = 60;
    if (!model.addZone(zone)) return 4;

    JustaSampleAudioProcessor processor;
    juce::AudioProcessor& hostProcessor = processor;
    hostProcessor.setRateAndBufferSizeDetails(48000.0, 256);
    hostProcessor.prepareToPlay(48000.0, 256);
    juce::String error;
    if (!processor.loadInstrument(model, &error))
    {
        std::cerr << "loadInstrument failed: " << error << '\n';
        return 5;
    }
    if (!processor.isMultisampleInstrumentLoaded()) return 6;

    juce::AudioBuffer<float> audio(2, 256);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    hostProcessor.processBlock(audio, midi);
    double energy = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int frame = 0; frame < audio.getNumSamples(); ++frame)
        {
            const auto value = audio.getSample(channel, frame);
            if (!std::isfinite(value)) return 7;
            energy += std::abs(double(value));
        }
    if (energy < 0.01)
    {
        std::cerr << "MIDI render produced no meaningful audio; energy=" << energy << '\n';
        return 8;
    }
    double timeEffectTailEnergy = 0.0;
    for (int block = 1; block < 80; ++block)
    {
        audio.clear(); midi.clear();
        hostProcessor.processBlock(audio, midi);
        if (block >= 70)
            for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                for (int frame = 0; frame < audio.getNumSamples(); ++frame)
                {
                    const auto value = audio.getSample(channel, frame);
                    if (!std::isfinite(value)) return 25;
                    timeEffectTailEnergy += std::abs(double(value));
                }
    }
    if (timeEffectTailEnergy < 0.001)
    {
        std::cerr << "Per-zone time-effect sends produced no wet tail.\n";
        return 26;
    }

    // Exercise zone editing and persist MPE + modulation matrix state.
    auto editableModel = processor.getInstrumentModelSnapshot();
    auto editedZone = editableModel.zones().front();
    editedZone.gainDb = -2.5f;
    juce::String zoneError;
    if (!processor.updateInstrumentZone(editedZone, &zoneError))
    {
        std::cerr << "Zone edit failed: " << zoneError << '\n';
        return 18;
    }
    if (std::abs(processor.getInstrumentModelSnapshot().zones().front().gainDb + 2.5f) > 0.001f) return 19;
    sampler::MPEZoneConfig mpe;
    mpe.layout = sampler::MPEZoneConfig::Layout::Upper;
    mpe.memberChannels = 8;
    mpe.pitchBendRangeSemitones = 24.0f;
    processor.setMPEZoneConfig(mpe);
    processor.configureModulationLfo(4.0f, sampler::Lfo::Shape::Triangle);
    sampler::ModulationMatrix matrix;
    if (!matrix.addRoute({ sampler::ModSource::Velocity, sampler::ModDestination::GainDb, 6.0f, true })) return 20;
    processor.setModulationMatrix(matrix);

    juce::MemoryBlock state;
    hostProcessor.getStateInformation(state);
    if (state.getSize() <= 8) return 9;
    processor.clearInstrument();
    if (processor.isMultisampleInstrumentLoaded()) return 10;
    hostProcessor.setStateInformation(state.getData(), int(state.getSize()));
    for (int attempt = 0; attempt < 500 && !processor.isMultisampleInstrumentLoaded(); ++attempt)
        juce::Thread::sleep(10);
    if (!processor.isMultisampleInstrumentLoaded())
    {
        std::cerr << "Instrument did not restore from processor state.\n";
        return 11;
    }
    const auto restoredMpe = processor.getMPEZoneConfig();
    if (restoredMpe.layout != sampler::MPEZoneConfig::Layout::Upper || restoredMpe.memberChannels != 8
        || std::abs(restoredMpe.pitchBendRangeSemitones - 24.0f) > 0.001f) return 21;
    if (std::abs(processor.getModulationLfoRate() - 4.0f) > 0.001f
        || processor.getModulationLfoShape() != sampler::Lfo::Shape::Triangle) return 22;
    const auto restoredMatrix = processor.getModulationMatrixSnapshot();
    if (restoredMatrix.size() != 1 || restoredMatrix.route(0) == nullptr
        || restoredMatrix.route(0)->destination != sampler::ModDestination::GainDb) return 23;
    if (std::abs(processor.getInstrumentModelSnapshot().zones().front().gainDb + 2.5f) > 0.001f) return 24;
    if (std::abs(processor.getInstrumentModelSnapshot().zones().front().reverbSend - 0.35f) > 0.001f
        || std::abs(processor.getInstrumentModelSnapshot().zones().front().delaySend - 0.25f) > 0.001f) return 27;

    audio.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    hostProcessor.processBlock(audio, midi);
    energy = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int frame = 0; frame < audio.getNumSamples(); ++frame)
        {
            const auto value = audio.getSample(channel, frame);
            if (!std::isfinite(value)) return 12;
            energy += std::abs(double(value));
        }
    if (energy < 0.01) return 13;

    // Exercise the real .jasz file path and its cancellable worker-thread loader.
    const auto presetFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("sampler-processor-integration.jasz");
    std::string presetError;
    if (!sampler::saveInstrumentFile(model, presetFile.getFullPathName().toStdString(), &presetError))
    {
        std::cerr << "Could not write .jasz integration fixture: " << presetError << '\n';
        return 14;
    }
    processor.clearInstrument();
    juce::StringArray searchRoots;
    searchRoots.add(sampleFile.getParentDirectory().getFullPathName());
    processor.loadInstrumentFileAsync(presetFile, searchRoots);
    for (int attempt = 0; attempt < 500 && !processor.isMultisampleInstrumentLoaded(); ++attempt)
        juce::Thread::sleep(10);
    presetFile.deleteFile();
    if (!processor.isMultisampleInstrumentLoaded())
    {
        std::cerr << "Async .jasz load failed: instrument was not activated before timeout.\n";
        return 15;
    }

    audio.clear();
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    hostProcessor.processBlock(audio, midi);
    energy = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int frame = 0; frame < audio.getNumSamples(); ++frame)
        {
            const auto value = audio.getSample(channel, frame);
            if (!std::isfinite(value)) return 16;
            energy += std::abs(double(value));
        }
    if (energy < 0.01) return 17;

    hostProcessor.releaseResources();
    std::cout << "PASS: .wav decoded -> MIDI rendered -> state restored -> .jasz loaded asynchronously -> MIDI rendered again (energy=" << energy << ")\n";
    return 0;
}
