#include "InstrumentMap.h"
#include "../Expansion/InstrumentFileIO.h"
#include "../Expansion/SampleAnalysis.h"

#include <algorithm>
#include <cmath>

namespace
{
const auto panel = juce::Colour(0xe020242b);
const auto panelRaised = juce::Colour(0xd9292f38);
const auto textMain = juce::Colour(0xffe7eaf0);
const auto textMuted = juce::Colour(0xff9ba5b4);
const auto amber = juce::Colour(0xffd99b43);

juce::String noteName(int midi)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[midi % 12]) + juce::String(midi / 12 - 1);
}
}

void InstrumentKeyMap::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xb3191d24));
    g.fillRoundedRectangle(bounds, 8.0f);
    const auto left = 8.0f, right = bounds.getWidth() - 8.0f;
    const auto width = right - left;
    const auto trackTop = 7.0f;
    const auto trackHeight = 7.0f;
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        const auto& zone = zones[i];
        const auto row = float(i % 8);
        const auto x = left + width * float(zone.keyLow) / 128.0f;
        const auto w = juce::jmax(2.0f, width * float(zone.keyHigh - zone.keyLow + 1) / 128.0f);
        const auto selected = zone.id == selectedZoneId.toStdString();
        auto colour = selected ? juce::Colour(0xffe0a34c) : juce::Colour::fromHSV(float((i * 37) % 360) / 360.0f, 0.48f, 0.72f, 0.88f);
        g.setColour(colour);
        g.fillRoundedRectangle(x, trackTop + row * (trackHeight + 2.0f), w, trackHeight, 2.0f);
        if (selected)
        {
            g.setColour(juce::Colours::white.withAlpha(0.75f));
            g.drawRoundedRectangle(x, trackTop + row * (trackHeight + 2.0f), w, trackHeight, 2.0f, 1.0f);
        }
    }

    const auto keyboardTop = trackTop + 8.0f * (trackHeight + 2.0f) + 6.0f;
    const auto keyboardHeight = juce::jmax(24.0f, bounds.getBottom() - keyboardTop - 17.0f);
    const auto keyWidth = width / 128.0f;
    for (int key = 0; key < 128; ++key)
    {
        const auto pitchClass = key % 12;
        const bool black = pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
        const auto x = left + float(key) * keyWidth;
        g.setColour(black ? juce::Colour(0xff454c57) : juce::Colour(0xffcbd1d9));
        g.fillRect(x, keyboardTop, keyWidth + 0.4f, black ? keyboardHeight * 0.64f : keyboardHeight);
        g.setColour(juce::Colour(0xff11151b));
        g.drawRect(x, keyboardTop, keyWidth, black ? keyboardHeight * 0.64f : keyboardHeight, 0.5f);
    }
    g.setColour(juce::Colour(0xff9ba5b4));
    g.setFont(juce::Font(9.0f));
    for (int key = 0; key < 128; key += 12)
    {
        const auto x = left + float(key) * keyWidth;
        g.drawText(noteName(key), int(x), int(keyboardTop + keyboardHeight + 1.0f), 28, 14, juce::Justification::centredLeft);
    }
}

void InstrumentKeyMap::mouseDown(const juce::MouseEvent& event)
{
    if (zones.empty() || getWidth() <= 0) return;
    const auto left = 8.0f, width = float(getWidth()) - 16.0f;
    const auto trackTop = 7.0f, trackStep = 9.0f;
    const auto keyboardTop = trackTop + 8.0f * trackStep + 6.0f;
    if (event.position.y < keyboardTop)
    {
        const auto row = juce::jlimit(0, 7, int((event.position.y - trackTop) / trackStep));
        const auto x = juce::jlimit(0.0f, 1.0f, (event.position.x - left) / width);
        const auto key = int(x * 128.0f);
        for (std::size_t i = 0; i < zones.size(); ++i)
            if (zones[i].keyLow <= key && key <= zones[i].keyHigh && int(i % 8) == row)
            { if (onZoneClicked) onZoneClicked(zones[i].id); return; }
    }
    else
    {
        const auto x = juce::jlimit(0.0f, 1.0f, (event.position.x - left) / width);
        const auto key = juce::jlimit(0, 127, int(x * 128.0f));
        for (const auto& zone : zones)
            if (zone.keyLow <= key && key <= zone.keyHigh)
            { if (onZoneClicked) onZoneClicked(zone.id); return; }
    }
}

InstrumentMapComponent::InstrumentMapComponent(JustaSampleAudioProcessor& p) : processor(p)
{
    titleLabel.setText("INSTRUMENT MAP", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(15.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, textMain);
    addAndMakeVisible(titleLabel);

    countLabel.setColour(juce::Label::textColourId, textMuted);
    countLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(countLabel);

    zoneList.setModel(this);
    zoneList.setRowHeight(42);
    zoneList.setColour(juce::ListBox::backgroundColourId, panel);
    zoneList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(zoneList);
    addAndMakeVisible(keyMap);
    keyMap.onZoneClicked = [this](const juce::String& id)
    {
        selectedZoneId = id;
        auto it = std::find_if(model.zones().begin(), model.zones().end(), [this](const auto& z) { return z.id == selectedZoneId.toStdString(); });
        if (it != model.zones().end()) zoneList.selectRow(int(std::distance(model.zones().begin(), it)));
        showSelectedZone(); keyMap.repaint();
    };

    inspectorLabel.setText("ZONE INSPECTOR", juce::dontSendNotification);
    inspectorLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    inspectorLabel.setColour(juce::Label::textColourId, textMain);
    addAndMakeVisible(inspectorLabel);

    auto addLabel = [this](juce::Label& label, const juce::String& name)
    {
        label.setText(name, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, textMuted);
        label.setFont(juce::Font(11.5f));
        addAndMakeVisible(label);
    };
    addLabel(sampleLabel, "SAMPLE"); addLabel(sampleValueLabel, "");
    addLabel(keyLowLabel, "KEY LOW"); addLabel(keyHighLabel, "KEY HIGH");
    addLabel(velocityLowLabel, "VEL LOW"); addLabel(velocityHighLabel, "VEL HIGH"); addLabel(rootLabel, "ROOT NOTE");
    addLabel(gainLabel, "GAIN dB"); addLabel(panLabel, "PAN");
    addLabel(filterLabel, "FILTER"); addLabel(cutoffLabel, "CUTOFF Hz");
    addLabel(resonanceLabel, "RESONANCE"); addLabel(driveLabel, "DRIVE dB");
    addLabel(reverbSendLabel, "REVERB SEND"); addLabel(delaySendLabel, "DELAY SEND");
    addLabel(rrLabel, "ROUND ROBIN GROUP"); addLabel(keyFadeInLabel, "KEY FADE IN"); addLabel(keyFadeOutLabel, "KEY FADE OUT");
    addLabel(velocityFadeInLabel, "VEL FADE IN"); addLabel(velocityFadeOutLabel, "VEL FADE OUT");
    addLabel(sampleStartLabel, "SAMPLE START (FRAMES)"); addLabel(sampleEndLabel, "SAMPLE END (FRAMES)");
    addLabel(loopStartLabel, "LOOP START (FRAMES)"); addLabel(loopEndLabel, "LOOP END (FRAMES)");
    addLabel(loopModeLabel, "LOOP MODE"); addLabel(loopCrossfadeLabel, "LOOP CROSSFADE (FRAMES)");

    for (auto* field : { &keyLow, &keyHigh, &velocityLow, &velocityHigh, &rootKey,
                         &gainDb, &pan, &cutoffHz, &resonance, &driveDb, &reverbSend, &delaySend,
                         &roundRobin, &keyFadeIn, &keyFadeOut, &velocityFadeIn, &velocityFadeOut,
                         &sampleStart, &sampleEnd, &loopStart, &loopEnd, &loopCrossfade })
    {
        styleField(*field);
        addAndMakeVisible(field);
    }
    filterMode.addItem("Off", 1); filterMode.addItem("Low-pass", 2);
    filterMode.addItem("High-pass", 3); filterMode.addItem("Band-pass", 4);
    filterMode.setColour(juce::ComboBox::backgroundColourId, panelRaised);
    filterMode.setColour(juce::ComboBox::textColourId, textMain);
    filterMode.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(filterMode);
    loopMode.addItem("Off", 1); loopMode.addItem("Forward", 2); loopMode.addItem("Ping-pong", 3);
    loopMode.setColour(juce::ComboBox::backgroundColourId, panelRaised);
    loopMode.setColour(juce::ComboBox::textColourId, textMain);
    loopMode.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(loopMode);

    styleButton(loadButton); styleButton(saveButton); styleButton(addButton);
    styleButton(sliceButton); styleButton(removeButton); styleButton(applyButton, true);
    addAndMakeVisible(loadButton); addAndMakeVisible(saveButton);
    addAndMakeVisible(addButton); addAndMakeVisible(sliceButton); addAndMakeVisible(removeButton); addAndMakeVisible(applyButton);
    loadButton.onClick = [this] { loadPreset(); };
    saveButton.onClick = [this] { savePreset(); };
    addButton.onClick = [this] { addSamples(); };
    removeButton.onClick = [this] { removeZone(); };
    sliceButton.onClick = [this] { sliceSelectedSample(); };
    applyButton.onClick = [this] { applyZone(); };
    refresh();
}

InstrumentMapComponent::~InstrumentMapComponent()
{
    analysisPool.removeAllJobs(true, 5000);
}

void InstrumentMapComponent::styleButton(juce::TextButton& button, bool primary)
{
    button.setColour(juce::TextButton::buttonColourId, primary ? amber : panelRaised);
    button.setColour(juce::TextButton::buttonOnColourId, amber.brighter(0.1f));
    button.setColour(juce::TextButton::textColourOffId, primary ? juce::Colour(0xff171a1f) : textMain);
    button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff171a1f));
    button.setConnectedEdges(0);
}

void InstrumentMapComponent::styleField(juce::TextEditor& field)
{
    field.setColour(juce::TextEditor::backgroundColourId, panelRaised);
    field.setColour(juce::TextEditor::textColourId, textMain);
    field.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    field.setColour(juce::TextEditor::focusedOutlineColourId, amber);
    field.setFont(juce::Font(13.0f));
    field.setJustification(juce::Justification::centred);
}

void InstrumentMapComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff171a20));
    auto bounds = getLocalBounds().toFloat().reduced(12.0f);
    g.setColour(panel);
    g.fillRoundedRectangle(bounds, 12.0f);
    g.setColour(juce::Colour(0x66ffffff));
    g.drawRoundedRectangle(bounds, 12.0f, 1.0f);
    g.setColour(juce::Colour(0x22ffffff));
    g.drawLine(bounds.getX() + 14.0f, bounds.getY() + 1.0f, bounds.getRight() - 14.0f, bounds.getY() + 1.0f, 1.0f);
    g.setColour(textMuted);
    g.setFont(juce::Font(11.0f));
    g.drawText(statusMessage, 16, getHeight() - 28, getWidth() - 32, 16, juce::Justification::centredLeft);
}

void InstrumentMapComponent::resized()
{
    auto area = getLocalBounds().reduced(22, 16);
    auto header = area.removeFromTop(34);
    titleLabel.setBounds(header.removeFromLeft(220));
    countLabel.setBounds(header.removeFromRight(130));
    area.removeFromTop(12);

    auto actions = area.removeFromBottom(42);
    loadButton.setBounds(actions.removeFromLeft(86)); actions.removeFromLeft(6);
    saveButton.setBounds(actions.removeFromLeft(86)); actions.removeFromLeft(10);
    addButton.setBounds(actions.removeFromLeft(112)); actions.removeFromLeft(6);
    sliceButton.setBounds(actions.removeFromLeft(104)); actions.removeFromLeft(6);
    removeButton.setBounds(actions.removeFromLeft(100));
    applyButton.setBounds(actions.removeFromRight(144));
    area.removeFromBottom(14);

    const auto narrow = getWidth() < 900;
    const auto leftWidth = juce::jlimit(190, 310, int(area.getWidth() * (narrow ? 0.34f : 0.31f)));
    auto listArea = area.removeFromLeft(leftWidth);
    zoneList.setBounds(listArea);
    area.removeFromLeft(18);

    auto inspector = area;
    keyMap.setBounds(inspector.removeFromTop(72));
    inspector.removeFromTop(8);
    inspectorLabel.setBounds(inspector.removeFromTop(26));
    inspector.removeFromTop(8);
    const auto colGap = 10;
    const auto rowH = 36;
    const auto fieldW = juce::jmax(54, (inspector.getWidth() - colGap * 3) / 4);
    auto place = [&](juce::Label& label, juce::Component& field, int column, int row, int span = 1)
    {
        const auto x = inspector.getX() + column * (fieldW + colGap);
        const auto y = inspector.getY() + (row + 1) * rowH;
        const auto width = fieldW * span + colGap * (span - 1);
        label.setBounds(x, y, width, 14);
        field.setBounds(x, y + 15, width, 21);
    };
    sampleLabel.setBounds(inspector.getX(), inspector.getY(), inspector.getWidth(), 14);
    sampleValueLabel.setBounds(inspector.getX(), inspector.getY() + 15, inspector.getWidth(), 21);
    place(keyLowLabel, keyLow, 0, 0); place(keyHighLabel, keyHigh, 1, 0);
    place(velocityLowLabel, velocityLow, 2, 0); place(velocityHighLabel, velocityHigh, 3, 0);
    place(rootLabel, rootKey, 0, 1); place(gainLabel, gainDb, 1, 1);
    place(panLabel, pan, 2, 1); place(filterLabel, filterMode, 3, 1);
    place(cutoffLabel, cutoffHz, 0, 2); place(resonanceLabel, resonance, 1, 2);
    place(driveLabel, driveDb, 2, 2); place(reverbSendLabel, reverbSend, 3, 2);
    place(delaySendLabel, delaySend, 0, 3); place(rrLabel, roundRobin, 1, 3);
    place(keyFadeInLabel, keyFadeIn, 2, 3); place(keyFadeOutLabel, keyFadeOut, 3, 3);
    place(velocityFadeInLabel, velocityFadeIn, 0, 4); place(velocityFadeOutLabel, velocityFadeOut, 1, 4);
    place(sampleStartLabel, sampleStart, 2, 4); place(sampleEndLabel, sampleEnd, 3, 4);
    place(loopStartLabel, loopStart, 0, 5); place(loopEndLabel, loopEnd, 1, 5);
    place(loopModeLabel, loopMode, 2, 5); place(loopCrossfadeLabel, loopCrossfade, 3, 5);
}

int InstrumentMapComponent::getNumRows() { return int(model.size()); }

void InstrumentMapComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= int(model.size())) return;
    if (selected) { g.setColour(panelRaised); g.fillRoundedRectangle(3.0f, 2.0f, float(width - 6), float(height - 4), 7.0f); }
    const auto& zone = model.zones()[std::size_t(row)];
    g.setColour(selected ? amber : textMain);
    g.setFont(juce::Font(12.5f, juce::Font::bold));
    g.drawText(juce::File(zone.sample.path).getFileName(), 12, 4, width - 24, 17, juce::Justification::centredLeft, true);
    g.setColour(textMuted);
    g.setFont(juce::Font(10.5f));
    g.drawText(noteName(zone.keyLow) + " – " + noteName(zone.keyHigh)
               + "   Vel " + juce::String(zone.velocityLow) + "–" + juce::String(zone.velocityHigh),
               12, 22, width - 24, 15, juce::Justification::centredLeft, true);
}

void InstrumentMapComponent::selectedRowsChanged(int row)
{
    if (row >= 0 && row < int(model.size()))
    {
        selectedZoneId = model.zones()[std::size_t(row)].id;
        keyMap.selectedZoneId = selectedZoneId;
        keyMap.repaint();
        showSelectedZone();
    }
}

void InstrumentMapComponent::refresh()
{
    model = processor.getInstrumentModelSnapshot();
    countLabel.setText(juce::String(model.size()) + " ZONES", juce::dontSendNotification);
    keyMap.zones = model.zones();
    keyMap.selectedZoneId = selectedZoneId;
    if (selectedZoneId.isEmpty() && model.size() > 0) selectedZoneId = model.zones().front().id;
    auto selected = std::find_if(model.zones().begin(), model.zones().end(), [this](const auto& z) { return z.id == selectedZoneId.toStdString(); });
    if (selected == model.zones().end() && model.size() > 0) { selectedZoneId = model.zones().front().id; selected = model.zones().begin(); }
    zoneList.updateContent();
    if (selected != model.zones().end())
    {
        const auto row = int(std::distance(model.zones().begin(), selected));
        zoneList.selectRow(row, juce::dontSendNotification);
        showSelectedZone();
        statusMessage = "Instrument ready  •  " + juce::String(model.size()) + " sample zones";
    }
    else
    {
        selectedZoneId.clear();
        statusMessage = "Drop or add samples to create your first instrument";
        showSelectedZone();
    }
    repaint();
}

void InstrumentMapComponent::showSelectedZone()
{
    updatingFields = true;
    keyMap.selectedZoneId = selectedZoneId;
    keyMap.repaint();
    const auto it = std::find_if(model.zones().begin(), model.zones().end(), [this](const auto& z) { return z.id == selectedZoneId.toStdString(); });
    const auto found = it != model.zones().end();
    for (auto* field : { &keyLow, &keyHigh, &velocityLow, &velocityHigh, &rootKey, &gainDb, &pan, &cutoffHz, &resonance, &driveDb, &reverbSend, &delaySend,
                         &roundRobin, &keyFadeIn, &keyFadeOut, &velocityFadeIn, &velocityFadeOut,
                         &sampleStart, &sampleEnd, &loopStart, &loopEnd, &loopCrossfade })
        field->setEnabled(found);
    loopMode.setEnabled(found);
    filterMode.setEnabled(found);
    removeButton.setEnabled(found);
    sliceButton.setEnabled(found);
    applyButton.setEnabled(found);
    if (found)
    {
        const auto& z = *it;
        sampleValueLabel.setText(juce::File(z.sample.path).getFileName(), juce::dontSendNotification);
        keyLow.setText(juce::String(z.keyLow), false); keyHigh.setText(juce::String(z.keyHigh), false);
        velocityLow.setText(juce::String(z.velocityLow), false); velocityHigh.setText(juce::String(z.velocityHigh), false);
        rootKey.setText(juce::String(z.rootKey), false); gainDb.setText(juce::String(z.gainDb, 2), false);
        pan.setText(juce::String(z.pan, 2), false); cutoffHz.setText(juce::String(z.filterCutoffHz, 1), false);
        resonance.setText(juce::String(z.filterResonance, 2), false); driveDb.setText(juce::String(z.driveDb, 1), false);
        reverbSend.setText(juce::String(z.reverbSend, 2), false);
        delaySend.setText(juce::String(z.delaySend, 2), false);
        roundRobin.setText(juce::String(z.roundRobinGroup), false);
        keyFadeIn.setText(juce::String(z.keyFadeIn), false); keyFadeOut.setText(juce::String(z.keyFadeOut), false);
        velocityFadeIn.setText(juce::String(z.velocityFadeIn), false); velocityFadeOut.setText(juce::String(z.velocityFadeOut), false);
        sampleStart.setText(juce::String(z.sampleStart), false);
        sampleEnd.setText(juce::String(z.sampleEnd == 0 ? z.sample.frames : z.sampleEnd), false);
        loopStart.setText(juce::String(z.loopStart), false); loopEnd.setText(juce::String(z.loopEnd), false);
        loopCrossfade.setText(juce::String(z.loopCrossfadeFrames), false);
        loopMode.setSelectedId(int(z.loopMode) + 1, juce::dontSendNotification);
        filterMode.setSelectedId(int(z.filterMode) + 1, juce::dontSendNotification);
    }
    else
    {
        sampleValueLabel.setText("No zone selected", juce::dontSendNotification);
        for (auto* field : { &keyLow, &keyHigh, &velocityLow, &velocityHigh, &rootKey, &gainDb, &pan, &cutoffHz, &resonance, &driveDb, &reverbSend, &delaySend,
                             &roundRobin, &keyFadeIn, &keyFadeOut, &velocityFadeIn, &velocityFadeOut,
                             &sampleStart, &sampleEnd, &loopStart, &loopEnd, &loopCrossfade }) field->clear();
        filterMode.setSelectedId(1, juce::dontSendNotification); loopMode.setSelectedId(1, juce::dontSendNotification);
    }
    updatingFields = false;
}

void InstrumentMapComponent::setStatus(const juce::String& message)
{
    statusMessage = message;
    repaint();
}

void InstrumentMapComponent::applyZone()
{
    if (updatingFields || selectedZoneId.isEmpty()) return;
    auto it = std::find_if(model.zones().begin(), model.zones().end(), [this](const auto& z) { return z.id == selectedZoneId.toStdString(); });
    if (it == model.zones().end()) return;
    auto zone = *it;
    auto parse = [](const juce::TextEditor& field, double fallback) { return field.getText().trim().isEmpty() ? fallback : field.getText().getDoubleValue(); };
    auto parseInteger = [&](const juce::TextEditor& field, int fallback, int minimum, int maximum)
    {
        return juce::jlimit(minimum, maximum, int(juce::jlimit(double(minimum), double(maximum), parse(field, fallback))));
    };
    auto parseFrame = [&](const juce::TextEditor& field, std::uint64_t fallback)
    {
        const auto value = field.getText().trim().isEmpty() ? double(fallback) : double(field.getText().getLargeIntValue());
        return std::uint64_t(juce::jlimit(0.0, double(INT_MAX), value));
    };
    const auto low = int(parse(keyLow, zone.keyLow)), high = int(parse(keyHigh, zone.keyHigh));
    const auto vLow = int(parse(velocityLow, zone.velocityLow)), vHigh = int(parse(velocityHigh, zone.velocityHigh));
    const auto root = int(parse(rootKey, zone.rootKey));
    if (low < 0 || high > 127 || low > high || vLow < 1 || vHigh > 127 || vLow > vHigh || root < low || root > high)
    { setStatus("Invalid range: root note must be inside the key range; velocity is 1–127."); return; }
    zone.keyLow = std::uint8_t(low); zone.keyHigh = std::uint8_t(high);
    zone.velocityLow = std::uint8_t(vLow); zone.velocityHigh = std::uint8_t(vHigh); zone.rootKey = std::uint8_t(root);
    zone.gainDb = float(parse(gainDb, zone.gainDb)); zone.pan = float(parse(pan, zone.pan));
    zone.filterMode = sampler::ZoneFilterMode(filterMode.getSelectedId() - 1);
    zone.filterCutoffHz = float(parse(cutoffHz, zone.filterCutoffHz));
    zone.filterResonance = float(parse(resonance, zone.filterResonance));
    zone.driveDb = float(parse(driveDb, zone.driveDb));
    zone.reverbSend = float(parse(reverbSend, zone.reverbSend));
    zone.delaySend = float(parse(delaySend, zone.delaySend));
    zone.roundRobinGroup = std::uint8_t(parseInteger(roundRobin, zone.roundRobinGroup, 0, 255));
    zone.keyFadeIn = std::uint8_t(parseInteger(keyFadeIn, zone.keyFadeIn, 0, 127));
    zone.keyFadeOut = std::uint8_t(parseInteger(keyFadeOut, zone.keyFadeOut, 0, 127));
    zone.velocityFadeIn = std::uint8_t(parseInteger(velocityFadeIn, zone.velocityFadeIn, 0, 127));
    zone.velocityFadeOut = std::uint8_t(parseInteger(velocityFadeOut, zone.velocityFadeOut, 0, 127));
    zone.sampleStart = parseFrame(sampleStart, zone.sampleStart);
    zone.sampleEnd = parseFrame(sampleEnd, zone.sampleEnd == 0 ? zone.sample.frames : zone.sampleEnd);
    zone.loopStart = parseFrame(loopStart, zone.loopStart);
    zone.loopEnd = parseFrame(loopEnd, zone.loopEnd);
    zone.loopCrossfadeFrames = std::uint32_t(parseInteger(loopCrossfade, int(zone.loopCrossfadeFrames), 0, INT_MAX));
    zone.loopMode = sampler::ZoneLoopMode(juce::jlimit(0, 2, loopMode.getSelectedId() - 1));
    juce::String error;
    if (!processor.updateInstrumentZone(zone, &error)) { setStatus("Could not apply zone: " + error); return; }
    refresh(); setStatus("Zone updated");
}

void InstrumentMapComponent::loadModelAsync(sampler::InstrumentModel next, juce::StringArray roots,
                                             std::function<void(bool, const juce::String&)> completion)
{
    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("jas-map-" + juce::Uuid().toString() + ".jasz");
    std::string error;
    if (!sampler::saveInstrumentFile(next, temp.getFullPathName().toStdString(), &error))
    { setStatus("Could not stage instrument: " + juce::String(error)); return; }
    setStatus("Loading and decoding sample zones…");
    juce::Component::SafePointer<InstrumentMapComponent> safe(this);
    processor.loadInstrumentFileAsync(temp, roots, [safe, temp, completion = std::move(completion)](bool ok, const juce::String& message)
    {
        temp.deleteFile();
        if (safe == nullptr) return;
        if (ok) safe->refresh();
        safe->setStatus(ok ? "Instrument loaded" : "Load failed: " + message);
        if (completion) completion(ok, message);
    });
}

void InstrumentMapComponent::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser>("Load sampler instrument", juce::File{}, "*.jasz");
    juce::Component::SafePointer<InstrumentMapComponent> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto file = fc.getResult(); if (!file.existsAsFile()) return;
            juce::StringArray roots; roots.add(file.getParentDirectory().getFullPathName());
            safe->processor.loadInstrumentFileAsync(file, roots, [safe](bool ok, const juce::String& message)
            { if (safe == nullptr) return; if (ok) safe->refresh(); safe->setStatus(ok ? "Instrument loaded" : "Load failed: " + message); });
            safe->setStatus("Loading instrument and decoding samples…");
        });
}

void InstrumentMapComponent::savePreset()
{
    if (model.size() == 0) { setStatus("Nothing to save"); return; }
    chooser = std::make_unique<juce::FileChooser>("Save sampler instrument", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("My Instrument.jasz"), "*.jasz");
    juce::Component::SafePointer<InstrumentMapComponent> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe](const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto file = fc.getResult(); if (file == juce::File{}) return;
            if (file.getFileExtension().isEmpty()) file = file.withFileExtension(".jasz");
            auto snapshot = safe->processor.getInstrumentModelSnapshot();
            std::string error;
            const auto ok = sampler::saveInstrumentFile(snapshot, file.getFullPathName().toStdString(), &error);
            safe->setStatus(ok ? "Saved " + file.getFileName() : "Save failed: " + juce::String(error));
        });
}

void InstrumentMapComponent::addSamples()
{
    chooser = std::make_unique<juce::FileChooser>("Add sample files to instrument", juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    juce::Component::SafePointer<InstrumentMapComponent> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
        [safe](const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            const auto files = fc.getResults(); if (files.isEmpty()) return;
            auto next = safe->processor.getInstrumentModelSnapshot();
            juce::AudioFormatManager formats; formats.registerBasicFormats();
            juce::StringArray roots;
            for (int fileIndex = 0; fileIndex < files.size(); ++fileIndex)
            {
                const auto file = files[fileIndex];
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
                if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > INT_MAX)
                { safe->setStatus("Skipped unreadable sample: " + file.getFileName()); continue; }
                sampler::SampleZone zone;
                zone.id = juce::Uuid().toString().toStdString(); zone.sample.id = zone.id;
                zone.sample.path = file.getFullPathName().toStdString(); zone.sample.sampleRate = reader->sampleRate;
                zone.sample.channels = std::uint32_t(reader->numChannels); zone.sample.frames = std::uint64_t(reader->lengthInSamples);
                const auto count = files.size();
                if (count == 1) { zone.keyLow = 0; zone.keyHigh = 127; zone.rootKey = 60; }
                else
                {
                    const auto mapLow = count <= 49 ? 36 : 0;
                    const auto mapHigh = count <= 49 ? 84 : 127;
                    const auto root = mapLow + fileIndex * (mapHigh - mapLow) / (count - 1);
                    const auto previous = fileIndex == 0 ? 0 : mapLow + (fileIndex - 1) * (mapHigh - mapLow) / (count - 1);
                    const auto following = fileIndex == count - 1 ? 127 : mapLow + (fileIndex + 1) * (mapHigh - mapLow) / (count - 1);
                    zone.rootKey = std::uint8_t(root);
                    zone.keyLow = std::uint8_t(fileIndex == 0 ? 0 : (previous + root) / 2 + 1);
                    zone.keyHigh = std::uint8_t(fileIndex == count - 1 ? 127 : (root + following) / 2);
                }
                if (next.size() >= sampler::MultisampleEngine::maxZones)
                { safe->setStatus("Instrument reached its 128-zone limit"); break; }
                if (!next.addZone(std::move(zone))) { safe->setStatus("Could not add the selected sample zone"); break; }
                roots.addIfNotAlreadyThere(file.getParentDirectory().getFullPathName());
            }
            if (next.size() == 0) { safe->setStatus("No readable samples were selected"); return; }
            safe->loadModelAsync(std::move(next), roots);
        });
}

void InstrumentMapComponent::sliceSelectedSample()
{
    const auto it = std::find_if(model.zones().begin(), model.zones().end(), [this](const auto& z) { return z.id == selectedZoneId.toStdString(); });
    if (it == model.zones().end()) { setStatus("Select a sample zone to slice"); return; }
    const auto selected = *it;
    auto next = model;
    juce::Component::SafePointer<InstrumentMapComponent> safe(this);
    setStatus("Analyzing transients and generating slices…");
    analysisPool.addJob([safe, selected, next = std::move(next)]() mutable
    {
        juce::String error;
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        const juce::File file(selected.sample.path);
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader || reader->lengthInSamples <= 0 || reader->lengthInSamples > 20000000)
            error = "Sample could not be read or exceeds the 20-million-frame analysis limit.";
        std::vector<sampler::SampleZone> slices;
        juce::StringArray roots;
        if (error.isEmpty())
        {
            const auto channelsToRead = juce::jlimit(1, 2, int(reader->numChannels));
            juce::AudioBuffer<float> audio(channelsToRead, int(reader->lengthInSamples));
            if (!reader->read(&audio, 0, audio.getNumSamples(), 0, true, channelsToRead > 1))
                error = "Could not decode the sample for transient analysis.";
            else
            {
                std::vector<const float*> channels;
                for (int channel = 0; channel < audio.getNumChannels(); ++channel)
                    channels.push_back(audio.getReadPointer(channel));
                sampler::TransientAnalysisOptions analysisOptions;
                analysisOptions.sensitivity = 0.48f;
                analysisOptions.windowMs = 5;
                analysisOptions.minimumIntervalMs = 60;
                sampler::TransientAnalysisResult analysis;
                std::string analysisError;
                if (!sampler::analyzeTransients(channels.data(), channels.size(), std::uint64_t(audio.getNumSamples()),
                                                reader->sampleRate, analysisOptions, analysis, &analysisError))
                    error = analysisError;
                else
                {
                    sampler::SliceGenerationOptions sliceOptions;
                    sliceOptions.firstKey = 36; sliceOptions.lastKey = 84;
                    sliceOptions.velocityLow = selected.velocityLow; sliceOptions.velocityHigh = selected.velocityHigh;
                    const auto zonesAfterReplacement = next.size() - 1;
                    if (zonesAfterReplacement >= sampler::MultisampleEngine::maxZones)
                        error = "The instrument has no remaining zone slots for slices.";
                    else
                        sliceOptions.maximumSlices = sampler::MultisampleEngine::maxZones - zonesAfterReplacement;
                    std::string sliceError;
                    if (error.isNotEmpty()) {}
                    else if (!sampler::buildSliceZones(analysis, selected.sample, sliceOptions, slices, &sliceError))
                        error = sliceError;
                    else
                    {
                        next.removeZone(selected.id);
                        for (auto& zone : slices)
                            if (!next.addZone(std::move(zone))) { error = "The instrument reached its 128-zone limit."; break; }
                        roots.add(file.getParentDirectory().getFullPathName());
                    }
                }
            }
        }
        juce::MessageManager::callAsync([safe, next = std::move(next), roots, error, sliceCount = slices.size()]() mutable
        {
            if (safe == nullptr) return;
            if (error.isNotEmpty()) { safe->setStatus("Slice analysis failed: " + error); return; }
            safe->setStatus("Created " + juce::String(sliceCount) + " slices; loading audio…");
            safe->loadModelAsync(std::move(next), roots);
        });
    });
}

void InstrumentMapComponent::removeZone()
{
    if (selectedZoneId.isEmpty()) return;
    auto next = processor.getInstrumentModelSnapshot();
    if (!next.removeZone(selectedZoneId.toStdString())) return;
    selectedZoneId.clear();
    juce::StringArray roots;
    for (const auto& zone : next.zones()) roots.addIfNotAlreadyThere(juce::File(zone.sample.path).getParentDirectory().getFullPathName());
    if (next.size() == 0) { processor.clearInstrument(); refresh(); setStatus("Instrument cleared"); return; }
    loadModelAsync(std::move(next), roots);
}
