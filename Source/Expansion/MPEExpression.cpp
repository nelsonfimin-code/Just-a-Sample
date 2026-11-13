#include "MPEExpression.h"
#include <algorithm>
#include <cmath>
namespace sampler {
MPEExpression::MPEExpression(MPEZoneConfig c){configure(c);}
void MPEExpression::configure(MPEZoneConfig c){c.memberChannels=std::min<std::uint8_t>(c.memberChannels,15);if(!std::isfinite(c.pitchBendRangeSemitones)||c.pitchBendRangeSemitones<0)c.pitchBendRangeSemitones=48;c.pitchBendRangeSemitones=std::min(c.pitchBendRangeSemitones,96.0f);config_=c;reset();}
float MPEExpression::normalize7(std::uint16_t v)noexcept{return float(std::min<std::uint16_t>(v,127))/127.0f;}
std::uint8_t MPEExpression::masterChannel()const noexcept{return config_.layout==MPEZoneConfig::Layout::Lower?1:16;}
bool MPEExpression::isMemberChannel(std::uint8_t ch)const noexcept{if(ch<1||ch>16||ch==masterChannel()||!config_.memberChannels)return false;if(config_.layout==MPEZoneConfig::Layout::Lower)return ch>=2&&ch<=std::uint8_t(1+config_.memberChannels);return ch<=15&&ch>=std::uint8_t(16-config_.memberChannels);}
void MPEExpression::process(const MPEEvent& e)noexcept{if(e.channel<1||e.channel>16||e.note>127)return;auto& s=channels_[e.channel-1];switch(e.type){case MPEEvent::Type::PitchBend:{auto raw=std::min<std::uint16_t>(e.value,16383);float n=raw<8192?float(int(raw)-8192)/8192.0f:float(int(raw)-8192)/8191.0f;s.pitchBendSemitones=n*config_.pitchBendRangeSemitones;break;}case MPEEvent::Type::ChannelPressure:s.pressure=normalize7(e.value);break;case MPEEvent::Type::PolyPressure:notePressure_[e.channel-1][e.note]=normalize7(e.value);break;case MPEEvent::Type::ControlChange:if(e.controller==74)s.slide=normalize7(e.value);if(e.controller==64)s.sustain=e.value>=64?1.0f:0.0f;break;}}
ChannelExpression MPEExpression::forChannel(std::uint8_t ch)const noexcept{return ch>=1&&ch<=16?channels_[ch-1]:ChannelExpression{};}
float MPEExpression::perNotePressure(std::uint8_t ch,std::uint8_t note)const noexcept{return ch>=1&&ch<=16?notePressure_[ch-1][note]:0.0f;}
void MPEExpression::reset()noexcept{channels_={};notePressure_={};}
}
