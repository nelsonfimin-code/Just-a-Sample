#pragma once

#include <JuceHeader.h>
#include "../Expansion/InstrumentModel.h"
#include "../PluginProcessor.h"
#include <functional>
#include <vector>

class InstrumentKeyMap final : public juce::Component
{
public:
    std::vector<sampler::SampleZone> zones;
    juce::String selectedZoneId;
    std::function<void(const juce::String&)> onZoneClicked;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
};

class InstrumentMapComponent final : public juce::Component,
                                    private juce::ListBoxModel
{
public:
    explicit InstrumentMapComponent(JustaSampleAudioProcessor& processor);
    ~InstrumentMapComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int row) override;
    void loadPreset();
    void savePreset();
    void addSamples();
    void applyZone();
    void removeZone();
    void sliceSelectedSample();
    void loadModelAsync(sampler::InstrumentModel model, juce::StringArray roots,
                        std::function<void(bool, const juce::String&)> completion = {});
    void showSelectedZone();
    void setStatus(const juce::String& message);
    static void styleButton(juce::TextButton& button, bool primary = false);
    static void styleField(juce::TextEditor& field);

    JustaSampleAudioProcessor& processor;
    sampler::InstrumentModel model;
    juce::String selectedZoneId;
    juce::String statusMessage { "No multisample instrument loaded" };

    juce::Label titleLabel, countLabel, inspectorLabel, sampleLabel, sampleValueLabel, keyLowLabel, keyHighLabel, velocityLowLabel, velocityHighLabel,
                rootLabel, gainLabel, panLabel, filterLabel, cutoffLabel, resonanceLabel, driveLabel, reverbSendLabel, delaySendLabel,
                rrLabel, keyFadeInLabel, keyFadeOutLabel, velocityFadeInLabel, velocityFadeOutLabel,
                sampleStartLabel, sampleEndLabel, loopStartLabel, loopEndLabel, loopModeLabel, loopCrossfadeLabel;
    juce::ListBox zoneList;
    InstrumentKeyMap keyMap;
    juce::TextEditor keyLow, keyHigh, velocityLow, velocityHigh, rootKey, gainDb, pan,
                     cutoffHz, resonance, driveDb, reverbSend, delaySend,
                     roundRobin, keyFadeIn, keyFadeOut, velocityFadeIn, velocityFadeOut,
                     sampleStart, sampleEnd, loopStart, loopEnd, loopCrossfade;
    juce::ComboBox filterMode, loopMode;
    juce::TextButton loadButton { "Load .jasz" }, saveButton { "Save .jasz" },
                     addButton { "+ Add samples" }, sliceButton { "Slice sample" }, removeButton { "Remove zone" },
                     applyButton { "Apply zone changes" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool analysisPool { 1 };
    bool updatingFields { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentMapComponent)
};
