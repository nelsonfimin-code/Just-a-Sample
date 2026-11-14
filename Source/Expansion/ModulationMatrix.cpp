#include "ModulationMatrix.h"
#include <algorithm>
#include <cmath>

namespace sampler {
bool ModulationMatrix::valid(const ModulationRoute& r) noexcept {
    return std::isfinite(r.depth) && r.depth >= -1.0e6f && r.depth <= 1.0e6f
        && static_cast<unsigned>(r.source) <= static_cast<unsigned>(ModSource::Envelope1)
        && static_cast<unsigned>(r.destination) <= static_cast<unsigned>(ModDestination::SampleStart);
}
bool ModulationMatrix::addRoute(ModulationRoute r) noexcept {
    if (count_ == maxRoutes || !valid(r)) return false;
    routes_[count_++] = r;
    return true;
}
bool ModulationMatrix::setRoute(std::size_t i, ModulationRoute r) noexcept {
    if (i >= count_ || !valid(r)) return false;
    routes_[i] = r;
    return true;
}
bool ModulationMatrix::removeRoute(std::size_t i) noexcept {
    if (i >= count_) return false;
    for (auto n = i + 1; n < count_; ++n) routes_[n - 1] = routes_[n];
    routes_[--count_] = {};
    return true;
}
void ModulationMatrix::clear() noexcept { routes_ = {}; count_ = 0; }
float ModulationMatrix::sourceValue(ModSource s, const ModulationInputs& i) noexcept {
    float v = 0.0f;
    switch (s) {
        case ModSource::Velocity: v = i.velocity; break;
        case ModSource::KeyTrack: v = i.keyTrack; break;
        case ModSource::ChannelPressure: v = i.channelPressure; break;
        case ModSource::PolyPressure: v = i.polyPressure; break;
        case ModSource::Slide: v = i.slide; break;
        case ModSource::PitchBend: v = i.pitchBend; break;
        case ModSource::ModWheel: v = i.modWheel; break;
        case ModSource::Aftertouch: v = i.aftertouch; break;
        case ModSource::Lfo1: v = i.lfo1; break;
        case ModSource::Envelope1: v = i.envelope1; break;
    }
    return std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
}
float ModulationMatrix::evaluate(ModDestination d, float base, float lo, float hi,
                                 const ModulationInputs& inputs) const noexcept {
    if (!std::isfinite(base) || !std::isfinite(lo) || !std::isfinite(hi) || lo > hi)
        return 0.0f;
    float result = base;
    for (std::size_t n = 0; n < count_; ++n) {
        const auto& r = routes_[n];
        if (r.enabled && r.destination == d)
            result += sourceValue(r.source, inputs) * r.depth;
    }
    return std::clamp(result, lo, hi);
}
void Lfo::configure(float rate, Shape shape) noexcept {
    rateHz_ = std::isfinite(rate) ? std::clamp(rate, 0.01f, 40.0f) : 1.0f;
    shape_ = shape;
}
void Lfo::reset(float phase) noexcept {
    phase_ = std::isfinite(phase) ? phase - std::floor(phase) : 0.0f;
    value_ = 0.0f;
}
float Lfo::next(float sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate <= 0.0f) return value_;
    phase_ += rateHz_ / sampleRate;
    phase_ -= std::floor(phase_);
    switch (shape_) {
        case Shape::Sine: value_ = std::sin(phase_ * 6.2831853071795864769f); break;
        case Shape::Triangle: value_ = 1.0f - 4.0f * std::abs(phase_ - 0.5f); break;
        case Shape::Saw: value_ = 2.0f * phase_ - 1.0f; break;
        case Shape::Square: value_ = phase_ < 0.5f ? 1.0f : -1.0f; break;
    }
    return value_;
}
}
