#pragma once
#include <array>
#include <cstdint>
#include "ModulationMatrix.h"
namespace sampler {
struct MPEZoneConfig{enum class Layout{Lower,Upper};Layout layout=Layout::Lower;std::uint8_t memberChannels=15;float pitchBendRangeSemitones=48.0f;};
struct ChannelExpression{float pitchBendSemitones=0,pressure=0,slide=0,sustain=0,modWheel=0;};
struct MPEEvent{enum class Type{PitchBend,ChannelPressure,PolyPressure,ControlChange};Type type{};std::uint8_t channel=1,note=0,controller=0;std::uint16_t value=0;};
class MPEExpression{
public:
 explicit MPEExpression(MPEZoneConfig={});
 void configure(MPEZoneConfig);
 const MPEZoneConfig& config()const noexcept{return config_;}
 void process(const MPEEvent&)noexcept;
 ChannelExpression forChannel(std::uint8_t)const noexcept;
 float perNotePressure(std::uint8_t,std::uint8_t)const noexcept;
 ModulationInputs modulationInputs(std::uint8_t channel,std::uint8_t note,float velocity,float keyTrack,float lfo=0,float envelope=0)const noexcept;
 bool isMemberChannel(std::uint8_t)const noexcept;
 void reset()noexcept;
private:
 static float normalize7(std::uint16_t)noexcept;
 std::uint8_t masterChannel()const noexcept;
 MPEZoneConfig config_;
 std::array<ChannelExpression,16> channels_{};
 std::array<std::array<float,128>,16> notePressure_{};
};
}
