#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class ModulationPanel final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit ModulationPanel(JustaSampleAudioProcessor& processor);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void refresh();
private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void selectedRowsChanged(int row) override;
    void updateMPE();
    void updateLfo();
    void addRoute();
    void removeRoute();
    void setSlider(juce::Slider& slider, double minimum, double maximum, double interval, const juce::String& suffix = {});
    void styleCombo(juce::ComboBox& combo);

    JustaSampleAudioProcessor& processor;
    sampler::ModulationMatrix matrix;
    juce::Label title, mpeTitle, lfoTitle, matrixTitle, layoutLabel, membersLabel, bendLabel, rateLabel, shapeLabel, sourceLabel, destinationLabel, depthLabel, status;
    juce::ComboBox mpeLayout, lfoShape, source, destination;
    juce::Slider members, bend, rate, depth;
    juce::ListBox routes;
    juce::TextButton add { "Add route" }, remove { "Remove route" };
    bool refreshing { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationPanel)
};
