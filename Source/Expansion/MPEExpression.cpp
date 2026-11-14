#include "MPEExpression.h"
#include <algorithm>
#include <cmath>
namespace sampler {
MPEExpression::MPEExpression(MPEZoneConfig c){configure(c);}
void MPEExpression::configure(MPEZoneConfig c){c.memberChannels=std::min<std::uint8_t>(c.memberChannels,15);if(!std::isfinite(c.pitchBendRangeSemitones)||c.pitchBendRangeSemitones<0)c.pitchBendRangeSemitones=48;c.pitchBendRangeSemitones=std::min(c.pitchBendRangeSemitones,96.0f);config_=c;reset();}
float MPEExpression::normalize7(std::uint16_t v)noexcept{return float(std::min<std::uint16_t>(v,127))/127.0f;}
std::uint8_t MPEExpression::masterChannel()const noexcept{return config_.layout==MPEZoneConfig::Layout::Lower?1:16;}
bool MPEExpression::isMemberChannel(std::uint8_t ch)const noexcept{if(ch<1||ch>16||ch==masterChannel()||!config_.memberChannels)return false;if(config_.layout==MPEZoneConfig::Layout::Lower)return ch>=2&&ch<=std::uint8_t(1+config_.memberChannels);return ch<=15&&ch>=std::uint8_t(16-config_.memberChannels);}
void MPEExpression::process(const MPEEvent& e)noexcept{if(e.channel<1||e.channel>16||e.note>127)return;auto& s=channels_[e.channel-1];switch(e.type){case MPEEvent::Type::PitchBend:{auto raw=std::min<std::uint16_t>(e.value,16383);float n=raw<8192?float(int(raw)-8192)/8192.0f:float(int(raw)-8192)/8191.0f;s.pitchBendSemitones=n*config_.pitchBendRangeSemitones;break;}case MPEEvent::Type::ChannelPressure:s.pressure=normalize7(e.value);break;case MPEEvent::Type::PolyPressure:notePressure_[e.channel-1][e.note]=normalize7(e.value);break;case MPEEvent::Type::ControlChange:if(e.controller==1)s.modWheel=normalize7(e.value);if(e.controller==74)s.slide=normalize7(e.value);if(e.controller==64)s.sustain=e.value>=64?1.0f:0.0f;break;}}
ChannelExpression MPEExpression::forChannel(std::uint8_t ch)const noexcept{
 if(ch<1||ch>16)return {};
 const auto member=channels_[ch-1];
 if(!isMemberChannel(ch))return member;
 const auto master=channels_[masterChannel()-1];
 ChannelExpression combined;
 combined.pitchBendSemitones=std::clamp(member.pitchBendSemitones+master.pitchBendSemitones,-2.0f*config_.pitchBendRangeSemitones,2.0f*config_.pitchBendRangeSemitones);
 combined.pressure=std::max(member.pressure,master.pressure);
 combined.slide=std::clamp(member.slide+master.slide,0.0f,1.0f);
 combined.sustain=(member.sustain>0.0f||master.sustain>0.0f)?1.0f:0.0f;
 combined.modWheel=std::max(member.modWheel,master.modWheel);
 return combined;
}
float MPEExpression::perNotePressure(std::uint8_t ch,std::uint8_t note)const noexcept{return ch>=1&&ch<=16&&note<128?notePressure_[ch-1][note]:0.0f;}
ModulationInputs MPEExpression::modulationInputs(std::uint8_t ch,std::uint8_t note,float velocity,float keyTrack,float lfo,float envelope)const noexcept{
 const auto e=forChannel(ch);ModulationInputs inputs;
 inputs.velocity=std::clamp(velocity,0.0f,1.0f);inputs.keyTrack=std::clamp(keyTrack,-1.0f,1.0f);
 inputs.channelPressure=e.pressure;inputs.polyPressure=perNotePressure(ch,note);inputs.slide=e.slide;
 inputs.pitchBend=config_.pitchBendRangeSemitones>0?std::clamp(e.pitchBendSemitones/config_.pitchBendRangeSemitones,-1.0f,1.0f):0.0f;
 inputs.modWheel=e.modWheel;inputs.aftertouch=e.pressure;inputs.lfo1=std::clamp(lfo,-1.0f,1.0f);inputs.envelope1=std::clamp(envelope,0.0f,1.0f);return inputs;
}
void MPEExpression::reset()noexcept{channels_={};notePressure_={};}
}
