#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sampler {
enum class ModSource : std::uint8_t {
    Velocity, KeyTrack, ChannelPressure, PolyPressure, Slide,
    PitchBend, ModWheel, Aftertouch, Lfo1, Envelope1
};
enum class ModDestination : std::uint8_t {
    PitchSemitones, GainDb, Pan, FilterCutoffHz, AttackMs, ReleaseMs, SampleStart
};
struct ModulationRoute {
    ModSource source = ModSource::Velocity;
    ModDestination destination = ModDestination::GainDb;
    float depth = 0.0f;
    bool enabled = true;
};
struct ModulationInputs {
    float velocity = 0.0f, keyTrack = 0.0f, channelPressure = 0.0f;
    float polyPressure = 0.0f, slide = 0.0f, pitchBend = 0.0f;
    float modWheel = 0.0f, aftertouch = 0.0f, lfo1 = 0.0f, envelope1 = 0.0f;
};
class ModulationMatrix {
public:
    static constexpr std::size_t maxRoutes = 64;
    bool addRoute(ModulationRoute) noexcept;
    bool setRoute(std::size_t, ModulationRoute) noexcept;
    bool removeRoute(std::size_t) noexcept;
    void clear() noexcept;
    std::size_t size() const noexcept { return count_; }
    const ModulationRoute* route(std::size_t i) const noexcept { return i < count_ ? &routes_[i] : nullptr; }
    float evaluate(ModDestination, float baseValue, float minimum, float maximum,
                   const ModulationInputs&) const noexcept;
    static float sourceValue(ModSource, const ModulationInputs&) noexcept;
private:
    static bool valid(const ModulationRoute&) noexcept;
    std::array<ModulationRoute, maxRoutes> routes_{};
    std::size_t count_ = 0;
};
class Lfo {
public:
    enum class Shape : std::uint8_t { Sine, Triangle, Saw, Square };
    void configure(float rateHz, Shape shape) noexcept;
    void reset(float phase = 0.0f) noexcept;
    float next(float sampleRate) noexcept;
    float value() const noexcept { return value_; }
private:
    float rateHz_ = 1.0f, phase_ = 0.0f, value_ = 0.0f;
    Shape shape_ = Shape::Sine;
};
}
