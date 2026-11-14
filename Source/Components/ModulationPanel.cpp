#include "ModulationPanel.h"
#include <algorithm>

namespace
{
const auto bg = juce::Colour(0xff171a20);
const auto panel = juce::Colour(0xe020242b);
const auto raised = juce::Colour(0xd9292f38);
const auto primary = juce::Colour(0xffe7eaf0);
const auto muted = juce::Colour(0xff9ba5b4);
const auto amber = juce::Colour(0xffd99b43);
const juce::StringArray sources { "Velocity", "Key Track", "Channel Pressure", "Poly Pressure", "Slide / CC74", "Pitch Bend", "Mod Wheel", "Aftertouch", "LFO 1", "Envelope 1" };
const juce::StringArray destinations { "Pitch (semitones)", "Gain (dB)", "Pan", "Filter Cutoff (Hz)", "Attack (ms)", "Release (ms)", "Sample Start" };
}

ModulationPanel::ModulationPanel(JustaSampleAudioProcessor& p) : processor(p)
{
    auto label = [this](juce::Label& l, const juce::String& text, bool heading = false)
    {
        l.setText(text, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, heading ? primary : muted);
        l.setFont(juce::Font(heading ? 14.0f : 11.5f, heading ? juce::Font::bold : juce::Font::plain));
        addAndMakeVisible(l);
    };
    label(title, "MODULATION WORKSPACE", true);
    label(mpeTitle, "MPE CONFIGURATION", true); label(lfoTitle, "LFO 1", true);
    label(matrixTitle, "MODULATION MATRIX", true);
    label(layoutLabel, "Zone layout"); label(membersLabel, "Member channels"); label(bendLabel, "Pitch bend range");
    label(rateLabel, "Rate (Hz)"); label(shapeLabel, "Waveform");
    label(sourceLabel, "Source"); label(destinationLabel, "Destination"); label(depthLabel, "Depth");
    label(status, "MPE expression and modulation routes are applied to the multisample engine.");

    mpeLayout.addItem("Lower zone", 1); mpeLayout.addItem("Upper zone", 2);
    lfoShape.addItem("Sine", 1); lfoShape.addItem("Triangle", 2); lfoShape.addItem("Saw", 3); lfoShape.addItem("Square", 4);
    for (int i = 0; i < sources.size(); ++i) source.addItem(sources[i], i + 1);
    for (int i = 0; i < destinations.size(); ++i) destination.addItem(destinations[i], i + 1);
    for (auto* combo : { &mpeLayout, &lfoShape, &source, &destination })
    { styleCombo(*combo); addAndMakeVisible(combo); }
    setSlider(members, 1, 15, 1); setSlider(bend, 1, 96, 1, " st");
    setSlider(rate, 0.01, 40, 0.01, " Hz"); setSlider(depth, -24000, 24000, 0.1);
    for (auto* slider : { &members, &bend, &rate, &depth }) addAndMakeVisible(slider);
    source.setSelectedId(1); destination.setSelectedId(2);
    routes.setModel(this); routes.setRowHeight(34); routes.setColour(juce::ListBox::backgroundColourId, panel);
    addAndMakeVisible(routes);
    for (auto* button : { &add, &remove })
    {
        button->setColour(juce::TextButton::buttonColourId, raised);
        button->setColour(juce::TextButton::textColourOffId, primary);
        addAndMakeVisible(button);
    }
    add.setColour(juce::TextButton::buttonColourId, amber);
    add.setColour(juce::TextButton::textColourOffId, bg);
    add.onClick = [this] { addRoute(); }; remove.onClick = [this] { removeRoute(); };
    mpeLayout.onChange = [this] { updateMPE(); };
    members.onValueChange = [this] { updateMPE(); }; bend.onValueChange = [this] { updateMPE(); };
    rate.onValueChange = [this] { updateLfo(); }; lfoShape.onChange = [this] { updateLfo(); };
    auto updateDepthRange = [this]
    {
        switch (destination.getSelectedId())
        {
            case 1: depth.setRange(-48.0, 48.0, 0.1); depth.setTextValueSuffix(" st"); break;
            case 2: depth.setRange(-24.0, 24.0, 0.1); depth.setTextValueSuffix(" dB"); break;
            case 3: depth.setRange(-1.0, 1.0, 0.01); depth.setTextValueSuffix(""); break;
            case 4: depth.setRange(-20000.0, 20000.0, 1.0); depth.setTextValueSuffix(" Hz"); break;
            case 5: case 6: depth.setRange(-5000.0, 5000.0, 1.0); depth.setTextValueSuffix(" ms"); break;
            default: depth.setRange(-1000000.0, 1000000.0, 1.0); depth.setTextValueSuffix(" frames"); break;
        }
    };
    source.onChange = updateDepthRange;
    destination.onChange = updateDepthRange;
    updateDepthRange();
    refresh();
}

void ModulationPanel::styleCombo(juce::ComboBox& combo)
{
    combo.setColour(juce::ComboBox::backgroundColourId, raised);
    combo.setColour(juce::ComboBox::textColourId, primary);
    combo.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
}

void ModulationPanel::setSlider(juce::Slider& slider, double minimum, double maximum, double interval, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 22);
    slider.setRange(minimum, maximum, interval);
    slider.setColour(juce::Slider::trackColourId, amber);
    slider.setColour(juce::Slider::thumbColourId, amber);
    slider.setColour(juce::Slider::textBoxTextColourId, primary);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, raised);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setTextValueSuffix(suffix);
}

void ModulationPanel::paint(juce::Graphics& g)
{
    g.fillAll(bg);
    auto area = getLocalBounds().toFloat().reduced(12.0f);
    g.setColour(panel); g.fillRoundedRectangle(area, 12.0f);
    g.setColour(juce::Colour(0x66ffffff)); g.drawRoundedRectangle(area, 12.0f, 1.0f);
    g.setColour(juce::Colour(0x22ffffff)); g.drawLine(area.getX() + 14.0f, area.getY() + 1.0f, area.getRight() - 14.0f, area.getY() + 1.0f, 1.0f);
}

void ModulationPanel::resized()
{
    auto area = getLocalBounds().reduced(26, 18);
    title.setBounds(area.removeFromTop(28)); area.removeFromTop(14);
    auto top = area.removeFromTop(190);
    auto left = top.removeFromLeft(top.getWidth() / 2 - 12);
    auto right = top;
    mpeTitle.setBounds(left.removeFromTop(24)); lfoTitle.setBounds(right.removeFromTop(24));
    auto placeRow = [](juce::Rectangle<int>& r, juce::Label& l, juce::Component& c)
    { auto row = r.removeFromTop(36); l.setBounds(row.removeFromLeft(130)); c.setBounds(row); };
    placeRow(left, layoutLabel, mpeLayout); placeRow(left, membersLabel, members); placeRow(left, bendLabel, bend);
    placeRow(right, rateLabel, rate); placeRow(right, shapeLabel, lfoShape);
    area.removeFromTop(14); matrixTitle.setBounds(area.removeFromTop(24)); area.removeFromTop(8);
    auto routing = area.removeFromTop(38);
    sourceLabel.setBounds(routing.removeFromLeft(54)); source.setBounds(routing.removeFromLeft(170)); routing.removeFromLeft(10);
    destinationLabel.setBounds(routing.removeFromLeft(78)); destination.setBounds(routing.removeFromLeft(190)); routing.removeFromLeft(10);
    depthLabel.setBounds(routing.removeFromLeft(40)); depth.setBounds(routing.removeFromLeft(180));
    area.removeFromTop(8);
    auto footer = area.removeFromBottom(34); add.setBounds(footer.removeFromLeft(112)); footer.removeFromLeft(8); remove.setBounds(footer.removeFromLeft(128));
    status.setBounds(footer.removeFromLeft(footer.getWidth()));
    routes.setBounds(area);
}

int ModulationPanel::getNumRows() { return int(matrix.size()); }
void ModulationPanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto* route = matrix.route(std::size_t(row)); if (!route) return;
    if (selected) { g.setColour(raised); g.fillRoundedRectangle(2.0f, 1.0f, float(width - 4), float(height - 2), 6.0f); }
    g.setColour(primary); g.setFont(juce::Font(12.0f));
    g.drawText(sources[int(route->source)] + "  →  " + destinations[int(route->destination)]
               + "     depth " + juce::String(route->depth, 2), 12, 0, width - 24, height, juce::Justification::centredLeft);
}
void ModulationPanel::selectedRowsChanged(int row)
{
    const auto* route = matrix.route(std::size_t(row)); if (!route) return;
    source.setSelectedId(int(route->source) + 1, juce::dontSendNotification);
    destination.setSelectedId(int(route->destination) + 1, juce::dontSendNotification);
    depth.setValue(route->depth, juce::dontSendNotification);
}

void ModulationPanel::refresh()
{
    refreshing = true;
    const auto config = processor.getMPEZoneConfig();
    mpeLayout.setSelectedId(config.layout == sampler::MPEZoneConfig::Layout::Lower ? 1 : 2, juce::dontSendNotification);
    members.setValue(config.memberChannels, juce::dontSendNotification);
    bend.setValue(config.pitchBendRangeSemitones, juce::dontSendNotification);
    rate.setValue(processor.getModulationLfoRate(), juce::dontSendNotification);
    lfoShape.setSelectedId(int(processor.getModulationLfoShape()) + 1, juce::dontSendNotification);
    matrix = processor.getModulationMatrixSnapshot();
    routes.updateContent();
    if (matrix.size() > 0) routes.selectRow(0, juce::dontSendNotification);
    refreshing = false;
}

void ModulationPanel::updateMPE()
{
    if (refreshing) return;
    sampler::MPEZoneConfig config;
    config.layout = mpeLayout.getSelectedId() == 2 ? sampler::MPEZoneConfig::Layout::Upper : sampler::MPEZoneConfig::Layout::Lower;
    config.memberChannels = std::uint8_t(juce::jlimit(1, 15, int(members.getValue())));
    config.pitchBendRangeSemitones = float(bend.getValue());
    processor.setMPEZoneConfig(config);
    status.setText("MPE settings applied", juce::dontSendNotification);
}

void ModulationPanel::updateLfo()
{
    if (refreshing) return;
    processor.configureModulationLfo(float(rate.getValue()), sampler::Lfo::Shape(juce::jlimit(0, 3, lfoShape.getSelectedId() - 1)));
    status.setText("LFO settings applied", juce::dontSendNotification);
}

void ModulationPanel::addRoute()
{
    auto next = processor.getModulationMatrixSnapshot();
    const auto dest = sampler::ModDestination(juce::jlimit(0, 6, destination.getSelectedId() - 1));
    if (!next.addRoute({ sampler::ModSource(juce::jlimit(0, 9, source.getSelectedId() - 1)), dest, float(depth.getValue()), true }))
    { status.setText("Maximum of 64 routes reached", juce::dontSendNotification); return; }
    processor.setModulationMatrix(next); matrix = next; routes.updateContent(); routes.selectRow(int(matrix.size()) - 1);
    status.setText("Modulation route added", juce::dontSendNotification);
}

void ModulationPanel::removeRoute()
{
    const auto row = routes.getSelectedRow();
    auto next = processor.getModulationMatrixSnapshot();
    if (row < 0 || !next.removeRoute(std::size_t(row))) return;
    processor.setModulationMatrix(next); matrix = next; routes.updateContent();
    status.setText("Modulation route removed", juce::dontSendNotification);
}
