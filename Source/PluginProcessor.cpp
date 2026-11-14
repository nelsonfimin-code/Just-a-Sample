/*
  ==============================================================================

    PluginProcessor.cpp
    Created: 5 Sep 2023
    Author:  binya

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginParameters.h"
#include "Expansion/InstrumentFileIO.h"
#include <climits>
#include <cstring>
#include "Sampler/CustomSynthesizer.h"

#if JUCE_DEBUG
#include "Utilities/BufferUtils.h"
#endif

JustaSampleAudioProcessor::JustaSampleAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
    apvts(*this, &undoManager, "Parameters", PluginParameters::createParameterLayout()),
    samplerSound(apvts, pluginState, sampleBuffer, int(bufferSampleRate)),
    fileFilter("", {}, {}),
    deviceRecorder(deviceManager)
#endif
{
    deviceRecorder.addListener(this);
    pitchDetector.addListener(this);

    formatManager.registerBasicFormats();
    fileFilter = juce::WildcardFileFilter(formatManager.getWildcardForAllFormats(), {}, {});

    mtsClient = MTS_RegisterClient();
}

JustaSampleAudioProcessor::~JustaSampleAudioProcessor()
{
    asyncLifetime->generation.fetch_add(1, std::memory_order_acq_rel);
    asyncLifetime->alive.store(false, std::memory_order_release);
    instrumentLoadPool.removeAllJobs(true, INT_MAX);

    for (int i = synth.getNumVoices() - 1; i >= 0; i--)
        synth.removeVoiceWithoutDeleting(i);

    MTS_DeregisterClient(mtsClient);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool JustaSampleAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

bool JustaSampleAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool JustaSampleAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool JustaSampleAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

juce::AudioProcessorEditor* JustaSampleAudioProcessor::createEditor()
{
    // A bit of a hack to set a default look and feel for the editor as it's created without sharing the object globally
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
    auto* editor = new JustaSampleAudioProcessorEditor(*this);
    editor->setLookAndFeel(&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);

    return editor;
}

juce::VST3ClientExtensions* JustaSampleAudioProcessor::getVST3ClientExtensions()
{
    if (PluginParameters::REAPER_INTEGRATION_ENABLED && hostType.isReaper() && wrapperType == wrapperType_VST3)
        return &reaperExtensions;

    return nullptr;
}

void JustaSampleAudioProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    juce::ScopedLock lock(voiceLock);

    synth.clearSounds();
    synth.addSound(new BlankSynthesizerSound());

    for (int i = synth.getNumVoices() - 1; i >= 0; i--)
        synth.removeVoiceWithoutDeleting(i);
    samplerVoices.clear();

    synth.setCurrentPlaybackSampleRate(sampleRate);
    multisampleEngine.reset();
    multisampleEngine.setOutputSampleRate(sampleRate);
    multisampleSends.setSize(4, juce::jmax(1, maximumExpectedSamplesPerBlock), false, false, true);
    multisampleSends.clear();
    juce::Reverb::Parameters reverbParameters;
    reverbParameters.roomSize = 0.62f;
    reverbParameters.damping = 0.48f;
    reverbParameters.wetLevel = 1.0f;
    reverbParameters.dryLevel = 0.0f;
    reverbParameters.width = 1.0f;
    reverbParameters.freezeMode = 0.0f;
    multisampleReverb.setSampleRate(sampleRate);
    multisampleReverb.setParameters(reverbParameters);
    const auto delayCapacity = juce::jmax(1, int(sampleRate * 1.5));
    multisampleDelayLeft.assign(std::size_t(delayCapacity), 0.0f);
    multisampleDelayRight.assign(std::size_t(delayCapacity), 0.0f);
    multisampleDelayFrames = juce::jlimit(1, delayCapacity - 1, int(sampleRate * 0.375));
    multisampleDelayWriteIndex = 0;

    for (int i = 0; i < PluginParameters::MAX_VOICES; i++)
    {
        const bool initializeSample = samplerSound.sampleRate > 0 && samplerSound.sample.getNumSamples() > 0;
        auto* voice = new CustomSamplerVoice(samplerSound, mtsClient, sampleRate, getBlockSize(), initializeSample);
        samplerVoices.add(voice);
    }
}

void JustaSampleAudioProcessor::releaseResources()
{
    // When playback stops, this is a place to clean up resources
}

void JustaSampleAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Handle VST3 Reaper extensions functionality
    if (PluginParameters::REAPER_INTEGRATION_ENABLED && hostType.isReaper() && wrapperType == wrapperType_VST3)
    {
        auto file = reaperExtensions.getNamedConfigParam(REAPER_FILE_PATH);
        juce::File filePath{ file };
        auto currentFile = lastLoadAttempt.isNotEmpty() ? lastLoadAttempt : juce::String(pluginState.filePath);
        juce::File currentFilePath{ currentFile };
        if (file.isNotEmpty() && filePath.getFileIdentifier() != currentFilePath.getFileIdentifier())
        {
            loadedFromReaper = true;
            loadSampleFromPath(file, true, "", false, [&](bool loaded) -> void
                {
                    if (!loaded)
                        loadedFromReaper = false;
                });
        }
    }

    juce::ScopedTryLock lock(voiceLock);

    if (lock.isLocked())
    {
        if (multiSampleMode.load(std::memory_order_acquire))
        {
            const auto hasTimeEffectBuses = multisampleSends.getNumSamples() >= buffer.getNumSamples();
            if (hasTimeEffectBuses) multisampleSends.clear();
            multisampleEngine.setVoiceLimit(std::size_t(juce::jlimit(1, PluginParameters::MAX_VOICES, int(p(PluginParameters::NUM_VOICES)))));
            int renderedTo = 0;
            for (const auto metadata : midiMessages)
            {
                const auto position = juce::jlimit(renderedTo, buffer.getNumSamples(), metadata.samplePosition);
                if (position > renderedTo)
                    multisampleEngine.render(buffer.getWritePointer(0) + renderedTo,
                        buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) + renderedTo : nullptr,
                        std::size_t(position - renderedTo), getSampleRate(),
                        hasTimeEffectBuses ? multisampleSends.getWritePointer(0) + renderedTo : nullptr,
                        hasTimeEffectBuses ? multisampleSends.getWritePointer(1) + renderedTo : nullptr,
                        hasTimeEffectBuses ? multisampleSends.getWritePointer(2) + renderedTo : nullptr,
                        hasTimeEffectBuses ? multisampleSends.getWritePointer(3) + renderedTo : nullptr);
                const auto& message = metadata.getMessage();
                sampler::EngineMidiEvent event;
                event.channel = std::uint8_t(message.getChannel());
                event.note = std::uint8_t(message.getNoteNumber());
                bool supported = true;
                if (message.isNoteOn()) { event.type = sampler::EngineMidiEvent::Type::NoteOn; event.value = std::uint16_t(juce::roundToInt(message.getFloatVelocity() * 127.0f)); }
                else if (message.isNoteOff()) { event.type = sampler::EngineMidiEvent::Type::NoteOff; }
                else if (message.isPitchWheel()) { event.type = sampler::EngineMidiEvent::Type::PitchBend; event.value = std::uint16_t(message.getPitchWheelValue()); }
                else if (message.isChannelPressure()) { event.type = sampler::EngineMidiEvent::Type::ChannelPressure; event.value = std::uint16_t(message.getChannelPressureValue()); }
                else if (message.isAftertouch()) { event.type = sampler::EngineMidiEvent::Type::PolyPressure; event.value = std::uint16_t(message.getAfterTouchValue()); }
                else if (message.isController()) { event.type = sampler::EngineMidiEvent::Type::ControlChange; event.controller = std::uint8_t(message.getControllerNumber()); event.value = std::uint16_t(message.getControllerValue()); }
                else supported = false;
                if (supported) multisampleEngine.handleMidi(event);
                renderedTo = position;
            }
            if (renderedTo < buffer.getNumSamples())
                multisampleEngine.render(buffer.getWritePointer(0) + renderedTo,
                    buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) + renderedTo : nullptr,
                    std::size_t(buffer.getNumSamples() - renderedTo), getSampleRate(),
                    hasTimeEffectBuses ? multisampleSends.getWritePointer(0) + renderedTo : nullptr,
                    hasTimeEffectBuses ? multisampleSends.getWritePointer(1) + renderedTo : nullptr,
                    hasTimeEffectBuses ? multisampleSends.getWritePointer(2) + renderedTo : nullptr,
                    hasTimeEffectBuses ? multisampleSends.getWritePointer(3) + renderedTo : nullptr);
            if (hasTimeEffectBuses)
            {
                auto* reverbL = multisampleSends.getWritePointer(0);
                auto* reverbR = multisampleSends.getWritePointer(1);
                multisampleReverb.processStereo(reverbL, reverbR, buffer.getNumSamples());
                auto* delayInL = multisampleSends.getReadPointer(2);
                auto* delayInR = multisampleSends.getReadPointer(3);
                const auto delayCapacity = int(multisampleDelayLeft.size());
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    const auto readIndex = (multisampleDelayWriteIndex - multisampleDelayFrames + delayCapacity) % delayCapacity;
                    const auto delayedL = multisampleDelayLeft[std::size_t(readIndex)];
                    const auto delayedR = multisampleDelayRight[std::size_t(readIndex)];
                    multisampleDelayLeft[std::size_t(multisampleDelayWriteIndex)] = delayInL[sample] + delayedL * 0.28f;
                    multisampleDelayRight[std::size_t(multisampleDelayWriteIndex)] = delayInR[sample] + delayedR * 0.28f;
                    multisampleDelayWriteIndex = (multisampleDelayWriteIndex + 1) % delayCapacity;
                    if (buffer.getNumChannels() > 1)
                    {
                        buffer.addSample(0, sample, reverbL[sample] + delayedL * 0.38f);
                        buffer.addSample(1, sample, reverbR[sample] + delayedR * 0.38f);
                    }
                    else
                        buffer.addSample(0, sample, 0.5f * (reverbL[sample] + reverbR[sample] + (delayedL + delayedR) * 0.38f));
                }
            }
        }
        else if (sampleBuffer.getNumSamples() > 0 && sampleBuffer.getNumChannels() > 0)
        {
            adjustVoiceCount();
            synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
        }

#if JUCE_DEBUG
        for (int ch = 0; ch < buffer.getNumChannels(); ch++)
        {
            protectYourEars(buffer.getWritePointer(ch), buffer.getNumSamples());
        }
#endif
    }
}

void JustaSampleAudioProcessor::adjustVoiceCount(int count)
{
    int numVoices = juce::jmax<int>(1, p(PluginParameters::NUM_VOICES));
    if (count >= 0)
        numVoices = count;
    int currentVoices = synth.getNumVoices();

    // We never need to instantiate new voices
    if (currentVoices > numVoices)
    {
        for (int i = currentVoices - 1; i >= numVoices; --i)
        {
            synth.removeVoiceWithoutDeleting(i);
            samplerVoices[i]->immediateHalt();
        }
    }
  
    if (currentVoices < numVoices)
    {
        for (int i = currentVoices; i < numVoices; ++i)
        {
            synth.addVoice(samplerVoices[i]);
        }
    }
}

//==============================================================================
void JustaSampleAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // All state properties should be saved at this point
    spv(PluginParameters::State::WIDTH) = pluginState.width.load();
    spv(PluginParameters::State::HEIGHT) = pluginState.height.load();
    spv(PluginParameters::State::FILE_PATH) = pluginState.filePath.load();
    spv(PluginParameters::State::SAMPLE_HASH) = pluginState.sampleHash.load();
    {
        juce::ScopedLock lock(voiceLock);
        std::string serializationError;
        const auto instrumentBytes = instrumentModel.size() > 0 ? instrumentModel.serialize(&serializationError) : std::vector<std::uint8_t>{};
        juce::MemoryBlock instrumentData(instrumentBytes.data(), instrumentBytes.size());
        spv(PluginParameters::State::INSTRUMENT_DATA) = instrumentBytes.empty() ? juce::String{} : instrumentData.toBase64Encoding();
    }
    spv(PluginParameters::State::USING_FILE_REFERENCE) = pluginState.usingFileReference.load();
    spv(PluginParameters::State::RECENT_FILES) = pluginState.recentFiles.load();
    if (const auto stateXml = deviceManager.createStateXml())
        spv(PluginParameters::State::SAVED_DEVICE_SETTINGS) = stateXml->toString();
    spv(PluginParameters::State::UI_VIEW_START) = pluginState.viewStart.load();
    spv(PluginParameters::State::UI_VIEW_END) = pluginState.viewEnd.load();
    spv(PluginParameters::State::PIN_VIEW) = pluginState.pinView.load();
    spv(PluginParameters::State::PRIMARY_CHANNEL) = pluginState.primaryChannel.load();
    spv(PluginParameters::State::SAMPLE_START) = pluginState.sampleStart.load();
    spv(PluginParameters::State::SAMPLE_END) = pluginState.sampleEnd.load();
    spv(PluginParameters::State::LOOP_START) = pluginState.loopStart.load();
    spv(PluginParameters::State::LOOP_END) = pluginState.loopEnd.load();
    spv(PluginParameters::State::SHOW_FX) = pluginState.showFX.load();
    spv(PluginParameters::State::DARK_MODE) = pluginState.darkMode.load();

    // Then, write empty "header" information to the stream
    size_t initialSize{ 0 };
    size_t apvtsSize{ 0 };
    {
        auto apvtsMos = juce::MemoryOutputStream{ destData, true };
        apvtsMos.writeInt(0);  // apvts size
        apvtsMos.writeInt(0);  // sample size
        initialSize = apvtsMos.getDataSize();

        apvts.state.writeToStream(apvtsMos);
        apvtsSize = apvtsMos.getDataSize() - initialSize;
    }

    size_t sampleSize = 0;

    // If we're not using a file reference, write the sample buffer to the stream
    if (!pluginState.usingFileReference && sampleBuffer.getNumSamples())
    {
        juce::WavAudioFormat wavFormat;
        auto options = juce::AudioFormatWriterOptions{}
            .withSampleRate(bufferSampleRate)
            .withNumChannels(sampleBuffer.getNumChannels())
            .withBitsPerSample(PluginParameters::STORED_BITRATE);

        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(destData, true);
        auto formatWriter = wavFormat.createWriterFor(stream, options);
        formatWriter->writeFromAudioSampleBuffer(sampleBuffer, 0, sampleBuffer.getNumSamples());
        formatWriter.reset();
        sampleSize = destData.getSize() - apvtsSize - initialSize;
    }

    // Write the header
    auto headerMos = juce::MemoryOutputStream{ destData, true };
    headerMos.setPosition(0);
    headerMos.writeInt(int(apvtsSize));
    headerMos.writeInt(int(sampleSize));
}

void JustaSampleAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // First, read the header information which includes the sizes of the APVTS and sample buffer
    juce::MemoryInputStream mis(data, sizeInBytes, false);
    size_t apvtsSize = mis.readInt();
    size_t sampleSize = mis.readInt();

    if (size_t(sizeInBytes) != apvtsSize + sampleSize + 8)
        return; // format issue

    // Read the APVTS
    juce::SubregionStream apvtsStream{ &mis, mis.getPosition(), juce::int64(apvtsSize), false };
    auto tree = juce::ValueTree::readFromStream(apvtsStream);
    if (tree.isValid())
    {
        apvts.replaceState(tree);

        sampler::MPEZoneConfig restoredMpe;
        restoredMpe.layout = int(apvts.state.getProperty("JAS MPE Layout", 0)) == 1
            ? sampler::MPEZoneConfig::Layout::Upper : sampler::MPEZoneConfig::Layout::Lower;
        restoredMpe.memberChannels = std::uint8_t(juce::jlimit(1, 15, int(apvts.state.getProperty("JAS MPE Members", 15))));
        restoredMpe.pitchBendRangeSemitones = float(apvts.state.getProperty("JAS MPE Bend Range", 48.0));
        setMPEZoneConfig(restoredMpe);
        const auto restoredShape = juce::jlimit(0, 3, int(apvts.state.getProperty("JAS LFO Shape", 0)));
        configureModulationLfo(float(apvts.state.getProperty("JAS LFO Rate", 1.0)), sampler::Lfo::Shape(restoredShape));
        sampler::ModulationMatrix restoredMatrix;
        const auto matrixState = apvts.state.getChildWithName("JASModulationMatrix");
        for (int i = 0; i < matrixState.getNumChildren(); ++i)
        {
            const auto item = matrixState.getChild(i);
            restoredMatrix.addRoute({ sampler::ModSource(juce::jlimit(0, int(sampler::ModSource::Envelope1), int(item.getProperty("source", 0)))),
                sampler::ModDestination(juce::jlimit(0, int(sampler::ModDestination::SampleStart), int(item.getProperty("destination", 0)))),
                float(item.getProperty("depth", 0.0)), bool(item.getProperty("enabled", true)) });
        }
        setModulationMatrix(restoredMatrix);

        const auto encodedInstrument = sp(PluginParameters::State::INSTRUMENT_DATA).toString();
        restoringInstrumentState.store(false, std::memory_order_release);
        if (encodedInstrument.isNotEmpty())
        {
            juce::MemoryBlock instrumentData;
            if (instrumentData.fromBase64Encoding(encodedInstrument))
            {
                std::vector<std::uint8_t> bytes(instrumentData.getSize());
                if (!bytes.empty()) std::memcpy(bytes.data(), instrumentData.getData(), bytes.size());
                sampler::InstrumentModel restoredInstrument;
                const auto validInstrument = restoredInstrument.deserialize(bytes);
                restoringInstrumentState.store(validInstrument, std::memory_order_release);
                if (validInstrument)
                    loadInstrumentModelAsync(std::move(restoredInstrument), [this](bool loaded, const juce::String&)
                    {
                        if (!loaded) restoringInstrumentState.store(false, std::memory_order_release);
                    });
            }
        }
        else if (multiSampleMode.load(std::memory_order_acquire))
            clearInstrument();
        const auto finishInstrumentRestore = [this]
        {
            restoringInstrumentState.store(false, std::memory_order_release);
        };

        // Load the plugin state struct
        pluginState.width = sp(PluginParameters::State::WIDTH);
        pluginState.height = sp(PluginParameters::State::HEIGHT);
        pluginState.filePath = sp(PluginParameters::State::FILE_PATH);
        pluginState.usingFileReference = sp(PluginParameters::State::USING_FILE_REFERENCE);
        pluginState.pinView = sp(PluginParameters::State::PIN_VIEW);
        pluginState.primaryChannel = sp(PluginParameters::State::PRIMARY_CHANNEL);
        pluginState.showFX = sp(PluginParameters::State::SHOW_FX);
        pluginState.darkMode = sp(PluginParameters::State::DARK_MODE);

        // We'd rather wait to update certain fields until the sample is actually loaded. This is usually irrelevant, but if the DAW
        // allows undo and redo then a previous sample will likely be loaded while the new one is loading, so the new info will not make sense.
        auto sampleHash = sp(PluginParameters::State::SAMPLE_HASH);
        auto updateFileInfo = [this, sampleHash,
            viewStart = sp(PluginParameters::State::UI_VIEW_START), viewEnd = sp(PluginParameters::State::UI_VIEW_END),
            sampleStart = sp(PluginParameters::State::SAMPLE_START), sampleEnd = sp(PluginParameters::State::SAMPLE_END),
            loopStart = sp(PluginParameters::State::LOOP_START), loopEnd = sp(PluginParameters::State::LOOP_END)]
            {
                pluginState.sampleHash = sampleHash;
                pluginState.viewStart = viewStart;
                pluginState.viewEnd = viewEnd;
                pluginState.sampleStart = sampleStart;
                pluginState.sampleEnd = sampleEnd;
                pluginState.loopStart = loopStart;
                pluginState.loopEnd = loopEnd;
            };

        bool newFile = pluginState.sampleHash != sp(PluginParameters::State::SAMPLE_HASH).toString();
        if (!newFile)
            updateFileInfo();

        auto recentFiles{ sp(PluginParameters::State::RECENT_FILES) };
        juce::StringArray fileArray{};
        for (int i = recentFiles.size() - 1; i >= 0; i--)
            fileArray.add(recentFiles[i].toString());
        pluginState.recentFiles = fileArray;

        juce::XmlDocument deviceSettingsDocument{ sp(PluginParameters::State::SAVED_DEVICE_SETTINGS) };
        deviceManagerLoadedState = deviceSettingsDocument.getDocumentElement();
        auto currentState = deviceManager.createStateXml();
        deviceManagerLoaded = currentState && currentState->isEquivalentTo(&*deviceManagerLoadedState, true);

        // Either load the sample from the file reference or from the stream directly
        juce::String filePath = pluginState.filePath;
        if (pluginState.usingFileReference && filePath.isNotEmpty() && newFile)
        {
            loadSampleFromPath(filePath, false, sampleHash, false, [this, filePath, updateFileInfo, sampleHash, finishInstrumentRestore](bool fileLoaded) -> void
                {
                    if (!fileLoaded)  // Either the file was not found, loaded incorrectly, or the hash was incorrect
                    {
                        openFileChooser("File was not found. Please locate " + juce::File(filePath).getFileName(),
                            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, updateFileInfo, sampleHash, finishInstrumentRestore](const juce::FileChooser& chooser)
                            {
                                auto file = chooser.getResult();
                                loadSampleFromPath(file.getFullPathName(), false, sampleHash, true,
                                    [updateFileInfo, finishInstrumentRestore](bool loaded) { if (loaded) updateFileInfo(); finishInstrumentRestore(); });
                            });
                    }
                    else
                    {
                        updateFileInfo();
                        finishInstrumentRestore();
                    }
                });
        }
        else if (sampleSize && newFile)
        {
            // A copy must be made to allow reading in another thread, outside the lifetime of this function
            auto sampleData = std::make_shared<juce::MemoryBlock>(sampleSize);
            mis.read(sampleData->getData(), int(sampleSize));
            auto wavStream = new juce::MemoryInputStream(*sampleData, false);

            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::AudioFormatReader> wavFormatReader{ wavFormat.createReaderFor(wavStream, true) };
            if (wavFormatReader)
            {
                lastLoadAttempt = "";
                reaperExtensions.setNamedConfigParam(REAPER_FILE_PATH, "");
                sampleLoader.loadSample(std::move(wavFormatReader), [this, sampleData /* necessary capture */, updateFileInfo, sampleHash, finishInstrumentRestore]
                (const std::unique_ptr<juce::AudioBuffer<float>>& loadedSample, const juce::String&, const std::unique_ptr<juce::AudioFormatReader>& reader) -> void
                    {
                        if (!reader)
                        {
                            finishInstrumentRestore();
                            return;
                        }

                        loadSample(*loadedSample, int(reader->sampleRate), false, sampleHash);
                        updateFileInfo();
                        finishInstrumentRestore();
                    });
            }
            else
                finishInstrumentRestore();
        }
        else
            finishInstrumentRestore();
    }
}

//==============================================================================
void JustaSampleAudioProcessor::loadSample(juce::AudioBuffer<float>& sample, int sampleRate, bool resetParameters, const juce::String& precomputedHash)
{
    juce::ScopedLock lock(voiceLock);

    haltVoices();
    if (!restoringInstrumentState.load(std::memory_order_acquire))
    {
        asyncLifetime->generation.fetch_add(1, std::memory_order_acq_rel);
        multisampleEngine.reset();
        instrumentSamples.clear();
        instrumentModel.clear();
        multiSampleMode.store(false, std::memory_order_release);
    }

    sampleBuffer = std::move(sample);
    bufferSampleRate = float(sampleRate);

    if (precomputedHash.isNotEmpty())
        pluginState.sampleHash = precomputedHash;
    else
        pluginState.sampleHash = getSampleHash(sampleBuffer);

    if (resetParameters)
    {
        pluginState.usingFileReference = sampleBufferNeedsReference();

        pluginState.sampleStart = 0;
        pluginState.sampleEnd = sampleBuffer.getNumSamples() - 1;

        // The order here is actually important, because the start and end buttons can otherwise enable the main loop button
        pv(PluginParameters::LOOPING_HAS_START) = false;
        pv(PluginParameters::LOOPING_HAS_END) = false;
        pv(PluginParameters::IS_LOOPING) = false;

        pluginState.loopStart = 0;
        pluginState.loopEnd = sampleBuffer.getNumSamples() - 1;
    }

    samplerSound.sampleChanged(int(bufferSampleRate));
    for (const auto& voice : samplerVoices)
        voice->initializeSample();
}

bool JustaSampleAudioProcessor::loadInstrument(const sampler::InstrumentModel& model, juce::String* error)
{
    asyncLifetime->generation.fetch_add(1, std::memory_order_acq_rel);
    return loadInstrumentInternal(model, error, 0);
}

void JustaSampleAudioProcessor::loadInstrumentModelAsync(
    sampler::InstrumentModel model,
    std::function<void(bool, const juce::String&)> callback)
{
    const auto generation = asyncLifetime->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    const auto weakLifetime = std::weak_ptr<AsyncLifetime>(asyncLifetime);
    instrumentLoadPool.addJob([this, generation, weakLifetime, model = std::move(model),
                               callback = std::move(callback)]() mutable
    {
        if (generation != asyncLifetime->generation.load(std::memory_order_acquire)) return;
        juce::String error;
        const auto loaded = loadInstrumentInternal(model, &error, generation);
        if (generation != asyncLifetime->generation.load(std::memory_order_acquire)) return;
        juce::MessageManager::callAsync([weakLifetime, generation, callback = std::move(callback), loaded, error]() mutable
        {
            const auto lifetime = weakLifetime.lock();
            if (lifetime == nullptr || !lifetime->alive.load(std::memory_order_acquire)
                || lifetime->generation.load(std::memory_order_acquire) != generation) return;
            if (callback) callback(loaded, error);
        });
    });
}

sampler::InstrumentModel JustaSampleAudioProcessor::getInstrumentModelSnapshot()
{
    juce::ScopedLock lock(voiceLock);
    return instrumentModel;
}

bool JustaSampleAudioProcessor::updateInstrumentZone(const sampler::SampleZone& zone, juce::String* error)
{
    auto fail = [error](const juce::String& message) { if (error) *error = message; return false; };
    juce::ScopedLock lock(voiceLock);
    if (!multiSampleMode.load(std::memory_order_acquire) || instrumentModel.size() == 0)
        return fail("Load a multisample instrument before editing zones.");
    auto candidate = instrumentModel;
    std::string validationError;
    if (!candidate.replaceZone(zone.id, zone, &validationError))
        return fail(validationError);
    if (instrumentSamples.size() != candidate.size())
        return fail("Instrument sample buffers are not aligned with the zone map.");
    std::array<sampler::ZoneAudio, sampler::MultisampleEngine::maxZones> audio{};
    for (std::size_t i = 0; i < candidate.size(); ++i)
    {
        const auto& buffer = instrumentSamples[i].buffer;
        if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
            return fail("A zone has no decoded sample buffer.");
        audio[i] = { &candidate.zones()[i], buffer.getReadPointer(0),
            buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr,
            std::uint64_t(buffer.getNumSamples()), instrumentSamples[i].sampleRate };
    }
    if (!multisampleEngine.setInstrument(candidate, audio.data(), candidate.size()))
        return fail("The updated zone map could not be prepared.");
    instrumentModel = std::move(candidate);
    return true;
}

bool JustaSampleAudioProcessor::loadInstrumentInternal(const sampler::InstrumentModel& model,
                                                        juce::String* error,
                                                        std::uint64_t expectedGeneration)
{
    auto fail = [error](const juce::String& message) { if (error) *error = message; return false; };
    if (model.size() == 0 || model.size() > sampler::MultisampleEngine::maxZones)
        return fail("Instrument must contain between 1 and 128 zones.");
    std::string validationError;
    if (!model.validate(&validationError)) return fail(validationError);

    constexpr std::uint64_t maximumDecodedSampleBytes = 512ull * 1024ull * 1024ull;
    std::uint64_t decodedSampleBytes = 0;
    std::vector<InstrumentSample> loaded;
    loaded.reserve(model.size());
    {
        juce::ScopedLock formatLock(formatManagerLock);
        for (const auto& zone : model.zones())
        {
            const juce::File file{ juce::String(zone.sample.path) };
            std::unique_ptr<juce::AudioFormatReader> reader{ formatManager.createReaderFor(file) };
            if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > INT_MAX || reader->numChannels <= 0)
                return fail("Could not read sample file: " + file.getFileName());
            const auto channelsToDecode = juce::jmin(2, int(reader->numChannels));
            const auto frames = std::uint64_t(reader->lengthInSamples);
            const auto bytesPerFrame = std::uint64_t(channelsToDecode) * sizeof(float);
            if (frames > maximumDecodedSampleBytes / bytesPerFrame)
                return fail("This instrument exceeds the 512 MiB decoded-sample memory limit.");
            const auto sampleBytes = frames * bytesPerFrame;
            if (decodedSampleBytes > maximumDecodedSampleBytes - sampleBytes)
                return fail("This instrument exceeds the 512 MiB decoded-sample memory limit.");
            decodedSampleBytes += sampleBytes;

            InstrumentSample sample;
            sample.sampleRate = reader->sampleRate;
            sample.buffer.setSize(channelsToDecode, int(reader->lengthInSamples));
            if (!reader->read(&sample.buffer, 0, int(reader->lengthInSamples), 0, true, true))
                return fail("Failed while decoding sample: " + file.getFileName());
            loaded.push_back(std::move(sample));
        }
    }

    juce::ScopedLock lock(voiceLock);
    if (expectedGeneration != 0
        && expectedGeneration != asyncLifetime->generation.load(std::memory_order_acquire))
        return fail("Instrument loading was cancelled.");
    juce::ScopedNoDenormals noDenormals;
    sampler::InstrumentModel candidate = model;
    std::vector<sampler::ZoneAudio> zoneAudio;
    zoneAudio.reserve(candidate.size());
    for (std::size_t i = 0; i < candidate.zones().size(); ++i)
    {
        auto& sample = loaded[i];
        auto& zone = candidate.zones()[i];
        zone.sample.sampleRate = sample.sampleRate;
        zone.sample.channels = std::uint32_t(sample.buffer.getNumChannels());
        zone.sample.frames = std::uint64_t(sample.buffer.getNumSamples());
        zoneAudio.push_back({ &zone, sample.buffer.getReadPointer(0),
            sample.buffer.getNumChannels() > 1 ? sample.buffer.getReadPointer(1) : nullptr,
            std::uint64_t(sample.buffer.getNumSamples()), sample.sampleRate });
    }
    if (!multisampleEngine.setInstrument(candidate, zoneAudio.data(), zoneAudio.size()))
        return fail("The instrument sample map could not be prepared.");
    auto* voiceParameter = apvts.getRawParameterValue(PluginParameters::NUM_VOICES);
    const auto voiceCount = voiceParameter != nullptr ? int(voiceParameter->load()) : 88;
    multisampleEngine.setVoiceLimit(std::size_t(juce::jlimit(1, PluginParameters::MAX_VOICES, voiceCount)));
    haltVoices();
    instrumentModel = std::move(candidate);
    instrumentSamples = std::move(loaded);
    multisampleEngine.setOutputSampleRate(getSampleRate());
    multiSampleMode.store(true, std::memory_order_release);
    return true;
}

void JustaSampleAudioProcessor::loadInstrumentFileAsync(
    const juce::File& presetFile,
    const juce::StringArray& searchRoots,
    std::function<void(bool, const juce::String&)> callback)
{
    const auto generation = asyncLifetime->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    const auto weakLifetime = std::weak_ptr<AsyncLifetime>(asyncLifetime);
    const auto presetPath = presetFile.getFullPathName();
    instrumentLoadPool.addJob([this, generation, weakLifetime, presetPath, searchRoots,
                               callback = std::move(callback)]() mutable
    {
        if (generation != asyncLifetime->generation.load(std::memory_order_acquire))
            return;

        sampler::InstrumentModel source;
        sampler::InstrumentModel resolved;
        sampler::RelinkReport relinkReport;
        std::string error;
        bool loaded = sampler::loadInstrumentFile(presetPath.toStdString(), source, &error);

        if (loaded && generation == asyncLifetime->generation.load(std::memory_order_acquire))
        {
            std::vector<std::string> roots;
            roots.reserve(std::size_t(searchRoots.size()));
            for (const auto& root : searchRoots)
                roots.push_back(root.toStdString());

            loaded = sampler::relinkMissingSamples(source, roots, resolved, relinkReport, &error);
            if (loaded && generation == asyncLifetime->generation.load(std::memory_order_acquire))
            {
                juce::String loadError;
                loaded = loadInstrumentInternal(resolved, &loadError, generation);
                if (!loaded)
                    error = loadError.toStdString();
            }
        }

        if (generation != asyncLifetime->generation.load(std::memory_order_acquire))
            return;

        const juce::String message(error);
        juce::MessageManager::callAsync([weakLifetime, generation, callback = std::move(callback),
                                         loaded, message]() mutable
        {
            const auto lifetime = weakLifetime.lock();
            if (lifetime == nullptr || !lifetime->alive.load(std::memory_order_acquire)
                || lifetime->generation.load(std::memory_order_acquire) != generation)
                return;
            if (callback)
                callback(loaded, message);
        });
    });
}

void JustaSampleAudioProcessor::cancelInstrumentLoad() noexcept
{
    asyncLifetime->generation.fetch_add(1, std::memory_order_acq_rel);
}

void JustaSampleAudioProcessor::setModulationMatrix(const sampler::ModulationMatrix& matrix)
{
    {
        juce::ScopedLock lock(voiceLock);
        modulationMatrix = matrix;
        multisampleEngine.setModulationMatrix(matrix);
    }
    juce::ValueTree state("JASModulationMatrix");
    for (std::size_t i = 0; i < matrix.size(); ++i)
    {
        const auto* route = matrix.route(i);
        if (!route) continue;
        juce::ValueTree item("Route");
        item.setProperty("source", int(route->source), nullptr);
        item.setProperty("destination", int(route->destination), nullptr);
        item.setProperty("depth", route->depth, nullptr);
        item.setProperty("enabled", route->enabled, nullptr);
        state.addChild(item, -1, nullptr);
    }
    apvts.state.removeChild(apvts.state.getChildWithName("JASModulationMatrix"), nullptr);
    apvts.state.addChild(state, -1, nullptr);
}

sampler::ModulationMatrix JustaSampleAudioProcessor::getModulationMatrixSnapshot()
{
    juce::ScopedLock lock(voiceLock);
    return modulationMatrix;
}

void JustaSampleAudioProcessor::setMPEZoneConfig(sampler::MPEZoneConfig config)
{
    {
        juce::ScopedLock lock(voiceLock);
        mpeZoneConfig = config;
        multisampleEngine.setMPEZoneConfig(config);
    }
    apvts.state.setProperty("JAS MPE Layout", int(config.layout), nullptr);
    apvts.state.setProperty("JAS MPE Members", int(config.memberChannels), nullptr);
    apvts.state.setProperty("JAS MPE Bend Range", config.pitchBendRangeSemitones, nullptr);
}

sampler::MPEZoneConfig JustaSampleAudioProcessor::getMPEZoneConfig()
{
    juce::ScopedLock lock(voiceLock);
    return mpeZoneConfig;
}

void JustaSampleAudioProcessor::configureModulationLfo(float rateHz, sampler::Lfo::Shape shape)
{
    {
        juce::ScopedLock lock(voiceLock);
        modulationLfoRateHz = juce::jlimit(0.01f, 40.0f, rateHz);
        modulationLfoShape = shape;
        multisampleEngine.configureLfo(modulationLfoRateHz, modulationLfoShape);
    }
    apvts.state.setProperty("JAS LFO Rate", modulationLfoRateHz, nullptr);
    apvts.state.setProperty("JAS LFO Shape", int(shape), nullptr);
}

float JustaSampleAudioProcessor::getModulationLfoRate()
{
    juce::ScopedLock lock(voiceLock);
    return modulationLfoRateHz;
}

sampler::Lfo::Shape JustaSampleAudioProcessor::getModulationLfoShape()
{
    juce::ScopedLock lock(voiceLock);
    return modulationLfoShape;
}

void JustaSampleAudioProcessor::clearInstrument()
{
    asyncLifetime->generation.fetch_add(1, std::memory_order_acq_rel);
    juce::ScopedLock lock(voiceLock);
    multisampleEngine.reset();
    instrumentSamples.clear();
    instrumentModel.clear();
    multiSampleMode = false;
}

void JustaSampleAudioProcessor::loadSampleFromPath(const juce::String& path, bool resetParameters, const juce::String& expectedHash, bool continueWithWrongHash, const std::function<void(bool)>& callback)
{
    const juce::File file{ path };
    std::unique_ptr<juce::AudioFormatReader> formatReader;
    {
        juce::ScopedLock formatLock(formatManagerLock);
        formatReader.reset(formatManager.createReaderFor(file));
    }

    if (!formatReader || !formatReader->lengthInSamples)
        return callback(false);

    lastLoadAttempt = path;
    reaperExtensions.setNamedConfigParam(REAPER_FILE_PATH, "");

    // Load the file and check the hash
    sampleLoader.loadSample(std::move(formatReader), [this, callback, path, expectedHash, continueWithWrongHash, resetParameters]
        (const std::unique_ptr<juce::AudioBuffer<float>>& loadedSample, const juce::String& sampleHash, const std::unique_ptr<juce::AudioFormatReader>& reader) -> void
        {
            if (expectedHash.isNotEmpty() && sampleHash != expectedHash && !continueWithWrongHash)
                return callback(false);

            pluginState.filePath = path;
            loadSample(*loadedSample, int(reader->sampleRate), resetParameters || (sampleHash != expectedHash && continueWithWrongHash), sampleHash);

            return callback(true);
        });
}

//==============================================================================
bool JustaSampleAudioProcessor::canLoadFileExtension(const juce::String& filePath) const
{
    return fileFilter.isFileSuitable(filePath);
}

bool JustaSampleAudioProcessor::sampleBufferNeedsReference(const juce::AudioBuffer<float>& buffer) const
{
    double totalFileBits = double(buffer.getNumSamples()) * buffer.getNumChannels() * PluginParameters::STORED_BITRATE;
    return totalFileBits > PluginParameters::MAX_FILE_SIZE;
}

//==============================================================================
void JustaSampleAudioProcessor::openFileChooser(const juce::String& message, int flags, const std::function<void(const juce::FileChooser&)>& callback, bool wavOnly)
{
    // DAW specific exceptions
    bool useNative = !hostType.isArdour();
    fileChooser = std::make_unique<juce::FileChooser>(message, juce::File::getSpecialLocation(juce::File::userDesktopDirectory), wavOnly ? "*.wav" : formatManager.getWildcardForAllFormats(), useNative);
    juce::MessageManager::callAsync([this, flags, callback] { fileChooser->launchAsync(flags, callback); });
}

void JustaSampleAudioProcessor::haltVoices()
{
    for (auto voice : samplerVoices)
    {
        voice->immediateHalt();
    }
}

void JustaSampleAudioProcessor::playVoice()
{
    synth.noteOn(0, 70, 1.0f);
}

void JustaSampleAudioProcessor::initializeDeviceManager()
{
    if (!deviceManagerLoaded)
    {
        deviceManager.initialise(2, 0, &*deviceManagerLoadedState, true);
        deviceManagerLoaded = true;
    }
}

void JustaSampleAudioProcessor::recordingFinished(juce::AudioBuffer<float> recordingBuffer, int recordingSampleRate)
{
    // If the recording is too large, we prompt to save it to a file, otherwise load it into the plugin simply
    if (sampleBufferNeedsReference(recordingBuffer))
    {
        openFileChooser("Recording too large for plugin state, save to a file",
                        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, 
            [this, recordingBuffer, recordingSampleRate](const juce::FileChooser& chooser) -> void {
                juce::File file = chooser.getResult();
                auto fileStream = std::make_unique<juce::FileOutputStream>(file);
                if (file.hasWriteAccess() && fileStream->openedOk())
                {
                    fileStream->setPosition(0);
                    fileStream->truncate();

                    juce::WavAudioFormat wavFormat;
                    auto options = juce::AudioFormatWriterOptions{}
                        .withSampleRate(recordingSampleRate)
                        .withNumChannels(recordingBuffer.getNumChannels())
                        .withBitsPerSample(PluginParameters::STORED_BITRATE);

                    std::unique_ptr<juce::OutputStream> outputStream = std::move(fileStream);
                    auto formatWriter = wavFormat.createWriterFor(outputStream, options);
                    formatWriter->writeFromAudioSampleBuffer(recordingBuffer, 0, recordingBuffer.getNumSamples());
                    outputStream.release();

                    loadSampleFromPath(file.getFullPathName(), true);
                }
            }, true);
    }
    else
    {
        pluginState.filePath = "";
        lastLoadAttempt = "";
        reaperExtensions.setNamedConfigParam(REAPER_FILE_PATH, "");
        loadSample(recordingBuffer, recordingSampleRate, true);
    }
}

bool JustaSampleAudioProcessor::startPitchDetectionRoutine(int startSample, int endSample)
{
    if (!sampleBuffer.getNumSamples())
        return false;

    // Truth be told, this does not need to be in a separate thread, but the algorithm I was using before was much slower
    pitchDetector.setData(sampleBuffer, startSample, endSample, bufferSampleRate);
    pitchDetector.startThread();
    return true;
}

void JustaSampleAudioProcessor::exitSignalSent()
{
    double pitch = pitchDetector.getPitch();
    if (pitch > 0)
    {
        float a4_hz = p(PluginParameters::A4_HZ);
        if (a4_hz <= 0)
            a4_hz = 440.0f;

        // Large pitch adjustments result in worse quality, so we limit to +/- 6 semitones for this feature
        float tuningAmount = 12.f * std::log2(a4_hz / pitch);
        float rangeMin = -6.f;  
        float rangeMax = 6.f;
        float range = rangeMax - rangeMin;
        tuningAmount = std::fmod(tuningAmount - rangeMin, range);
        if (tuningAmount < 0)
            tuningAmount += range;
        tuningAmount += rangeMin;

        int semitones = int(tuningAmount);
        int cents = int(100 * (tuningAmount - semitones));

        apvts.getParameterAsValue(PluginParameters::SEMITONE_TUNING) = semitones;
        apvts.getParameterAsValue(PluginParameters::CENT_TUNING) = cents;
        apvts.getParameterAsValue(PluginParameters::WIDE_TUNING) = 0.0f;
    }
}

//==============================================================================
int JustaSampleAudioProcessor::visibleSamples() const
{
    return pluginState.viewEnd.load() - pluginState.viewStart.load() + 1;
}

//==============================================================================
// This creates new instances of the plugin...
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JustaSampleAudioProcessor();
}
