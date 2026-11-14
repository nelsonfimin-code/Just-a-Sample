#include "MultisampleEngine.h"

#include <algorithm>
#include <cmath>

namespace sampler
{
namespace
{
constexpr float pi = 3.14159265358979323846f;

float readInterpolated(const float* samples, double position, double endFrame) noexcept
{
    const auto index = static_cast<std::uint64_t>(position);
    const auto last = static_cast<std::uint64_t>(endFrame) - 1;
    const auto next = std::min(index + 1, last);
    const auto fraction = static_cast<float>(position - static_cast<double>(index));
    return samples[index] + (samples[next] - samples[index]) * fraction;
}
}

bool MultisampleEngine::setInstrument(const InstrumentModel& model, const ZoneAudio* audio, std::size_t count)
{
    if (count > maxZones || model.size() > maxZones || model.size() != count || (count && audio == nullptr))
        return false;

    std::string error;
    if (!model.validate(&error))
        return false;

    InstrumentModel candidate = model;
    std::array<ZoneAudio, maxZones> prepared{};
    std::size_t preparedCount = 0;

    for (const auto& zone : candidate.zones())
    {
        const auto it = std::find_if(audio, audio + count, [&](const ZoneAudio& item)
        {
            return item.zone && item.zone->id == zone.id;
        });

        if (it == audio + count || !it->left || it->frames == 0
            || !std::isfinite(it->sampleRate) || it->sampleRate <= 0)
            return false;

        prepared[preparedCount] = *it;
        prepared[preparedCount].zone = &zone;
        ++preparedCount;
    }

    reset();
    instrument_ = std::move(candidate);
    audio_ = prepared;
    audioCount_ = preparedCount;
    return true;
}

void MultisampleEngine::reset() noexcept
{
    for (auto& voice : voices_)
        voice = {};
    mpe_.reset();
}

void MultisampleEngine::setOutputSampleRate(double rate) noexcept
{
    if (std::isfinite(rate) && rate > 0.0)
        outputRate_ = rate;
}

void MultisampleEngine::setVoiceLimit(std::size_t voices) noexcept
{
    voiceLimit_ = std::clamp<std::size_t>(voices, 1, maxVoices);
    for (std::size_t i = voiceLimit_; i < maxVoices; ++i)
        voices_[i].active = false;
}

void MultisampleEngine::setMPEZoneConfig(MPEZoneConfig config) noexcept
{
    mpe_.configure(config);
    refreshExpression(0);
}

MultisampleEngine::Voice* MultisampleEngine::allocateVoice() noexcept
{
    for (std::size_t i = 0; i < voiceLimit_; ++i)
        if (!voices_[i].active)
            return &voices_[i];

    auto it = std::min_element(voices_.begin(), voices_.begin() + std::ptrdiff_t(voiceLimit_),
        [](const Voice& a, const Voice& b) { return a.releaseGain < b.releaseGain; });
    return it == voices_.end() ? nullptr : &*it;
}

ModulationInputs MultisampleEngine::inputsFor(const Voice& voice, float lfoValue) const noexcept
{
    auto inputs = mpe_.modulationInputs(voice.channel, voice.note, voice.velocity,
                                        voice.keyTrack, lfoValue, voice.releaseGain);
    inputs.polyPressure = voice.pressure;
    inputs.slide = voice.slide;
    return inputs;
}

void MultisampleEngine::noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity) noexcept
{
    if (channel < 1 || channel > 16 || velocity == 0)
        return;

    const auto matches = instrument_.select(note, velocity);
    const auto expression = mpe_.forChannel(channel);

    for (const auto& match : matches)
    {
        if (!match.zone)
            continue;

        const auto audio = std::find_if(audio_.begin(), audio_.begin() + std::ptrdiff_t(audioCount_),
            [&](const ZoneAudio& item) { return item.zone == match.zone; });
        if (audio == audio_.begin() + std::ptrdiff_t(audioCount_))
            continue;

        auto* voice = allocateVoice();
        if (!voice)
            continue;

        const auto totalFrames = std::min<std::uint64_t>(audio->frames, match.zone->sample.frames);
        if (totalFrames == 0)
            continue;

        const auto sampleStart = std::min<std::uint64_t>(match.zone->sampleStart, totalFrames - 1);
        const auto sampleEnd = match.zone->sampleEnd == 0
            ? totalFrames : std::min(match.zone->sampleEnd, totalFrames);
        if (sampleEnd <= sampleStart)
            continue;

        voice->audio = &*audio;
        voice->channel = channel;
        voice->note = note;
        voice->velocity = static_cast<float>(velocity) / 127.0f;
        voice->keyTrack = std::clamp((static_cast<float>(note) - 60.0f) / 60.0f, -1.0f, 1.0f);
        voice->step = std::pow(2.0, double(float(int(note) - int(match.zone->rootKey))
                                         + expression.pitchBendSemitones) / 12.0)
                    * audio->sampleRate / (outputRate_ > 0 ? outputRate_ : audio->sampleRate);
        voice->gain = match.velocityGain * match.keyGain * voice->velocity * voice->velocity;
        voice->zoneGainDb = match.zone->gainDb;
        voice->pan = match.zone->pan;
        voice->filterMode = match.zone->filterMode;
        voice->filterCutoffHz = match.zone->filterCutoffHz;
        voice->filterResonance = match.zone->filterResonance;
        voice->driveDb = match.zone->driveDb;
        voice->reverbSend = match.zone->reverbSend;
        voice->delaySend = match.zone->delaySend;
        voice->pressure = std::max(expression.pressure, mpe_.perNotePressure(channel, note));
        voice->slide = expression.slide;
        voice->sampleStart = static_cast<double>(sampleStart);
        voice->sampleEnd = static_cast<double>(sampleEnd);
        voice->loopStart = static_cast<double>(std::clamp(match.zone->loopStart, sampleStart, sampleEnd));
        voice->loopEnd = static_cast<double>(std::clamp(match.zone->loopEnd, sampleStart, sampleEnd));
        voice->loopCrossfadeFrames = match.zone->loopCrossfadeFrames;
        voice->loopMode = match.zone->loopMode;
        voice->direction = 1.0f;
        voice->lfo.configure(lfoRateHz_, lfoShape_);
        voice->lfo.reset();

        const auto inputs = inputsFor(*voice, 0.0f);
        const auto start = modulation_.evaluate(ModDestination::SampleStart, 0.0f, 0.0f, 1.0f, inputs);
        voice->position = voice->sampleStart
                        + double(start) * std::max(0.0, voice->sampleEnd - voice->sampleStart - 1.0);

        const auto attackMs = modulation_.evaluate(ModDestination::AttackMs, 0.0f, 0.0f, 5000.0f, inputs);
        voice->attackGain = attackMs > 0.0f ? 0.0f : 1.0f;
        voice->attackStep = attackMs > 0.0f
            ? 1.0f / float((outputRate_ > 0 ? outputRate_ : 44100.0) * double(attackMs) / 1000.0)
            : 0.0f;

        voice->filterL = voice->filterR = voice->bandL = voice->bandR = 0.0f;
        voice->releaseGain = 1.0f;
        voice->releaseStep = 0.0f;
        voice->active = true;
        voice->releasing = false;
        voice->keyDown = true;
        voice->sustainHeld = false;
    }
}

void MultisampleEngine::noteOff(std::uint8_t channel, std::uint8_t note) noexcept
{
    const auto expression = mpe_.forChannel(channel);

    for (auto& voice : voices_)
    {
        if (!voice.active || voice.channel != channel || voice.note != note)
            continue;

        voice.keyDown = false;
        voice.sustainHeld = expression.sustain > 0.0f;
        if (expression.sustain <= 0.0f)
        {
            const auto inputs = inputsFor(voice, voice.lfo.value());
            const auto releaseMs = modulation_.evaluate(ModDestination::ReleaseMs, 80.0f, 1.0f, 5000.0f, inputs);
            voice.releasing = true;
            voice.loopMode = ZoneLoopMode::Off;
            voice.sustainHeld = false;
            voice.releaseStep = 1.0f / float((outputRate_ > 0 ? outputRate_ : 44100.0)
                                              * double(releaseMs) / 1000.0);
        }
    }
}

void MultisampleEngine::refreshExpression(std::uint8_t channel) noexcept
{
    for (auto& voice : voices_)
    {
        if (!voice.active || !(channel == 0 || voice.channel == channel || mpe_.isMemberChannel(voice.channel)))
            continue;

        const auto expression = mpe_.forChannel(voice.channel);
        voice.pressure = std::max(expression.pressure, mpe_.perNotePressure(voice.channel, voice.note));
        voice.slide = expression.slide;
        const auto semitones = float(int(voice.note) - int(voice.audio->zone->rootKey))
                             + expression.pitchBendSemitones;
        voice.step = std::pow(2.0, double(semitones) / 12.0) * voice.audio->sampleRate
                   / (outputRate_ > 0 ? outputRate_ : voice.audio->sampleRate);
    }
}

void MultisampleEngine::handleMidi(const EngineMidiEvent& event) noexcept
{
    if (event.channel < 1 || event.channel > 16)
        return;

    switch (event.type)
    {
        case EngineMidiEvent::Type::NoteOn:
            if (event.value > 0)
                noteOn(event.channel, event.note, std::uint8_t(std::min<std::uint16_t>(event.value, 127)));
            else
                noteOff(event.channel, event.note);
            return;

        case EngineMidiEvent::Type::NoteOff:
            noteOff(event.channel, event.note);
            return;

        case EngineMidiEvent::Type::PitchBend:
            mpe_.process({ MPEEvent::Type::PitchBend, event.channel, event.note, 0, event.value });
            refreshExpression(event.channel);
            return;

        case EngineMidiEvent::Type::ChannelPressure:
            mpe_.process({ MPEEvent::Type::ChannelPressure, event.channel, event.note, 0, event.value });
            refreshExpression(event.channel);
            return;

        case EngineMidiEvent::Type::PolyPressure:
            mpe_.process({ MPEEvent::Type::PolyPressure, event.channel, event.note, 0, event.value });
            refreshExpression(event.channel);
            return;

        case EngineMidiEvent::Type::ControlChange:
            mpe_.process({ MPEEvent::Type::ControlChange, event.channel, event.note,
                           event.controller, event.value });
            if (event.controller == 64 && event.value < 64)
            {
                for (auto& voice : voices_)
                {
                    if (!voice.active || voice.keyDown
                        || !(voice.channel == event.channel || mpe_.isMemberChannel(voice.channel))
                        || mpe_.forChannel(voice.channel).sustain > 0.0f)
                        continue;

                    const auto inputs = inputsFor(voice, voice.lfo.value());
                    const auto releaseMs = modulation_.evaluate(ModDestination::ReleaseMs, 80.0f,
                                                                 1.0f, 5000.0f, inputs);
                    voice.releasing = true;
                    voice.loopMode = ZoneLoopMode::Off;
                    voice.sustainHeld = false;
                    voice.releaseStep = 1.0f / float((outputRate_ > 0 ? outputRate_ : 44100.0)
                                                      * double(releaseMs) / 1000.0);
                }
            }
            if (event.controller == 1 || event.controller == 74 || event.controller == 64)
                refreshExpression(event.channel);
            return;
    }
}

void MultisampleEngine::render(float* left, float* right, std::size_t samples, double rate,
                               float* reverbLeft, float* reverbRight,
                               float* delayLeft, float* delayRight) noexcept
{
    if (!left || !std::isfinite(rate) || rate <= 0.0)
        return;

    outputRate_ = rate;

    for (auto& voice : voices_)
    {
        if (!voice.active || !voice.audio)
            continue;

        const auto& audio = *voice.audio;
        const auto totalFrames = std::min<std::uint64_t>(audio.frames, audio.zone->sample.frames);
        if (totalFrames == 0 || voice.sampleEnd <= voice.sampleStart)
        {
            voice.active = false;
            continue;
        }

        const auto* rightSamples = audio.right ? audio.right : audio.left;
        for (std::size_t i = 0; i < samples; ++i)
        {
            if (voice.loopMode == ZoneLoopMode::Forward
                && (voice.keyDown || voice.sustainHeld)
                && voice.position >= voice.loopStart
                && voice.loopEnd > voice.loopStart)
            {
                if (voice.position >= voice.loopEnd)
                {
                    const auto loopLength = voice.loopEnd - voice.loopStart;
                    const auto crossfade = std::min<double>(voice.loopCrossfadeFrames, loopLength * 0.5);
                    const auto effectiveLength = std::max(1.0, loopLength - crossfade);
                    voice.position = voice.loopStart + crossfade
                                   + std::fmod(voice.position - voice.loopEnd, effectiveLength);
                }
            }
            else if (voice.loopMode == ZoneLoopMode::PingPong
                     && (voice.keyDown || voice.sustainHeld)
                     && voice.position >= voice.loopStart
                     && voice.loopEnd - voice.loopStart > 1.0)
            {
                const auto span = voice.loopEnd - voice.loopStart - 1.0;
                const auto period = 2.0 * span;
                auto phase = voice.direction > 0.0f
                    ? voice.position - voice.loopStart
                    : period - (voice.position - voice.loopStart);
                phase = std::fmod(phase, period);
                if (phase < 0.0)
                    phase += period;
                if (phase <= span)
                {
                    voice.direction = 1.0f;
                    voice.position = voice.loopStart + phase;
                }
                else
                {
                    voice.direction = -1.0f;
                    voice.position = voice.loopStart + (period - phase);
                }
            }

            if (voice.position < voice.sampleStart || voice.position >= voice.sampleEnd)
            {
                voice.active = false;
                break;
            }

            const auto inputs = inputsFor(voice, voice.lfo.next(float(rate)));
            const auto pitchMod = modulation_.evaluate(ModDestination::PitchSemitones, 0.0f,
                                                        -96.0f, 96.0f, inputs);
            const auto gainDb = modulation_.evaluate(ModDestination::GainDb, voice.zoneGainDb,
                                                      -96.0f, 24.0f, inputs);
            const auto pan = modulation_.evaluate(ModDestination::Pan, voice.pan, -1.0f, 1.0f, inputs);
            const auto cutoffBase = voice.filterMode == ZoneFilterMode::Off
                ? float(rate * 0.49) : voice.filterCutoffHz;
            const auto cutoff = modulation_.evaluate(ModDestination::FilterCutoffHz, cutoffBase,
                                                      20.0f, float(rate * 0.49), inputs);
            const auto filterEnabled = voice.filterMode != ZoneFilterMode::Off
                || cutoff < float(rate * 0.48);
            const auto effectiveFilterMode = voice.filterMode == ZoneFilterMode::Off
                ? (filterEnabled ? ZoneFilterMode::LowPass : ZoneFilterMode::Off)
                : voice.filterMode;
            const auto safeCutoff = std::clamp(cutoff, 20.0f, float(rate * 0.45));
            const auto g = std::tan(pi * safeCutoff / float(rate));
            const auto q = 0.70710678f + voice.filterResonance * 11.29289322f;
            const auto k = 1.0f / q;
            const auto a1 = 1.0f / (1.0f + g * (g + k));
            const auto a2 = g * a1;
            const auto a3 = g * a2;

            auto rawL = readInterpolated(audio.left, voice.position, voice.sampleEnd);
            auto rawR = readInterpolated(rightSamples, voice.position, voice.sampleEnd);

            if (voice.loopMode == ZoneLoopMode::Forward
                && (voice.keyDown || voice.sustainHeld) && voice.loopCrossfadeFrames > 0)
            {
                const auto crossfade = std::min<double>(voice.loopCrossfadeFrames,
                                                        (voice.loopEnd - voice.loopStart) * 0.5);
                const auto crossfadeStart = voice.loopEnd - crossfade;
                if (crossfade > 0.0 && voice.position >= crossfadeStart)
                {
                    const auto mix = float(std::clamp((voice.position - crossfadeStart) / crossfade, 0.0, 1.0));
                    const auto loopPosition = voice.loopStart + (voice.position - crossfadeStart);
                    const auto loopL = readInterpolated(audio.left, loopPosition, voice.sampleEnd);
                    const auto loopR = readInterpolated(rightSamples, loopPosition, voice.sampleEnd);
                    rawL = rawL * (1.0f - mix) + loopL * mix;
                    rawR = rawR * (1.0f - mix) + loopR * mix;
                }
            }

            const auto v3L = rawL - voice.bandL - k * voice.filterL;
            const auto v1L = a1 * voice.filterL + a2 * v3L;
            const auto v2L = voice.bandL + a2 * voice.filterL + a3 * v3L;
            voice.filterL = 2.0f * v1L - voice.filterL;
            voice.bandL = 2.0f * v2L - voice.bandL;
            const auto v3R = rawR - voice.bandR - k * voice.filterR;
            const auto v1R = a1 * voice.filterR + a2 * v3R;
            const auto v2R = voice.bandR + a2 * voice.filterR + a3 * v3R;
            voice.filterR = 2.0f * v1R - voice.filterR;
            voice.bandR = 2.0f * v2R - voice.bandR;
            float filteredL = rawL;
            float filteredR = rawR;
            switch (effectiveFilterMode)
            {
                case ZoneFilterMode::Off: break;
                case ZoneFilterMode::LowPass:
                    filteredL = v2L;
                    filteredR = v2R;
                    break;
                case ZoneFilterMode::HighPass:
                    filteredL = rawL - k * v1L - v2L;
                    filteredR = rawR - k * v1R - v2R;
                    break;
                case ZoneFilterMode::BandPass:
                    filteredL = v1L;
                    filteredR = v1R;
                    break;
            }
            if (voice.driveDb > 0.001f)
            {
                const auto drive = std::pow(10.0f, voice.driveDb / 20.0f);
                const auto normalizer = std::tanh(drive);
                filteredL = std::tanh(filteredL * drive) / normalizer;
                filteredR = std::tanh(filteredR * drive) / normalizer;
            }

            const auto leftGain = std::cos((pan + 1.0f) * pi * 0.25f);
            const auto rightGain = std::sin((pan + 1.0f) * pi * 0.25f);
            const auto pressureGain = 1.0f + 0.1f * voice.pressure;
            const auto gain = voice.gain * std::pow(10.0f, gainDb / 20.0f) * pressureGain
                            * voice.attackGain * voice.releaseGain;

            const auto outputL = filteredL * gain * leftGain;
            const auto outputR = filteredR * gain * rightGain;
            left[i] += outputL;
            if (right) right[i] += outputR;
            if (reverbLeft) reverbLeft[i] += outputL * voice.reverbSend;
            if (reverbRight) reverbRight[i] += outputR * voice.reverbSend;
            if (delayLeft) delayLeft[i] += outputL * voice.delaySend;
            if (delayRight) delayRight[i] += outputR * voice.delaySend;

            const auto pitchStep = voice.step * std::exp2(pitchMod / 12.0f);
            if (voice.loopMode == ZoneLoopMode::PingPong
                && (voice.keyDown || voice.sustainHeld)
                && voice.position >= voice.loopStart
                && voice.loopEnd - voice.loopStart > 1.0)
            {
                const auto span = voice.loopEnd - voice.loopStart - 1.0;
                const auto period = 2.0 * span;
                auto phase = voice.direction > 0.0f
                    ? voice.position - voice.loopStart
                    : period - (voice.position - voice.loopStart);
                phase = std::fmod(phase + pitchStep, period);
                if (phase <= span)
                {
                    voice.direction = 1.0f;
                    voice.position = voice.loopStart + phase;
                }
                else
                {
                    voice.direction = -1.0f;
                    voice.position = voice.loopStart + (period - phase);
                }
            }
            else
            {
                voice.position += pitchStep * double(voice.direction);
                if (voice.loopMode == ZoneLoopMode::Forward
                    && (voice.keyDown || voice.sustainHeld)
                    && voice.position >= voice.loopEnd)
                {
                    const auto loopLength = voice.loopEnd - voice.loopStart;
                    const auto crossfade = std::min<double>(voice.loopCrossfadeFrames, loopLength * 0.5);
                    const auto effectiveLength = std::max(1.0, loopLength - crossfade);
                    voice.position = voice.loopStart + crossfade
                                   + std::fmod(voice.position - voice.loopEnd, effectiveLength);
                }
            }

            if (voice.attackGain < 1.0f)
                voice.attackGain = std::min(1.0f, voice.attackGain + voice.attackStep);

            if (voice.releasing)
            {
                voice.releaseGain = std::max(0.0f, voice.releaseGain - voice.releaseStep);
                if (voice.releaseGain <= 0.0f)
                {
                    voice.active = false;
                    break;
                }
            }
        }
    }
}

std::size_t MultisampleEngine::activeVoices() const noexcept
{
    return std::size_t(std::count_if(voices_.begin(), voices_.end(),
        [](const Voice& voice) { return voice.active; }));
}
}
