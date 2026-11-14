#pragma once
#include "InstrumentModel.h"
#include "MPEExpression.h"
#include "ModulationMatrix.h"
#include <array>
#include <cstddef>
#include <cstdint>
namespace sampler {
struct ZoneAudio { const SampleZone* zone=nullptr;const float* left=nullptr;const float* right=nullptr;std::uint64_t frames=0;double sampleRate=44100.0; };
struct EngineMidiEvent { enum class Type : std::uint8_t { NoteOn,NoteOff,PitchBend,ChannelPressure,PolyPressure,ControlChange };Type type=Type::NoteOn;std::uint8_t channel=1,note=0,controller=0;std::uint16_t value=0; };
class MultisampleEngine {
public:
 static constexpr std::size_t maxZones=128,maxVoices=256;
 bool setInstrument(const InstrumentModel&,const ZoneAudio*,std::size_t);
 void reset() noexcept;
 void setOutputSampleRate(double rate) noexcept;
 void setVoiceLimit(std::size_t voices) noexcept;
 void setMPEZoneConfig(MPEZoneConfig config) noexcept;
 void setModulationMatrix(const ModulationMatrix& matrix) noexcept { modulation_=matrix; }
 void configureLfo(float rateHz,Lfo::Shape shape) noexcept { lfoRateHz_=rateHz;lfoShape_=shape; }
 void handleMidi(const EngineMidiEvent&) noexcept;
 void render(float* left,float* right,std::size_t samples,double outputRate,
             float* reverbLeft=nullptr,float* reverbRight=nullptr,
             float* delayLeft=nullptr,float* delayRight=nullptr) noexcept;
 std::size_t activeVoices()const noexcept;
private:
 struct Voice {
  const ZoneAudio* audio=nullptr;
  std::uint8_t channel=1,note=0;
  double position=0,step=1,sampleStart=0,sampleEnd=0,loopStart=0,loopEnd=0;
  std::uint32_t loopCrossfadeFrames=0;
  ZoneLoopMode loopMode=ZoneLoopMode::Off;
  float direction=1.0f,gain=1,zoneGainDb=0,pan=0,velocity=0,keyTrack=0,pressure=0,slide=0;
  float filterL=0,filterR=0,bandL=0,bandR=0,releaseGain=1,releaseStep=0,attackGain=1,attackStep=0;
  ZoneFilterMode filterMode=ZoneFilterMode::Off;
  float filterCutoffHz=20000.0f,filterResonance=0.0f,driveDb=0.0f;
  float reverbSend=0.0f,delaySend=0.0f;
  Lfo lfo;
  bool active=false,releasing=false,keyDown=true,sustainHeld=false;
 };
 Voice* allocateVoice()noexcept;
 void noteOn(std::uint8_t,std::uint8_t,std::uint8_t)noexcept;
 void noteOff(std::uint8_t,std::uint8_t)noexcept;
 void refreshExpression(std::uint8_t)noexcept;
 ModulationInputs inputsFor(const Voice&,float lfoValue)const noexcept;
 std::array<ZoneAudio,maxZones> audio_{};std::size_t audioCount_=0;
 InstrumentModel instrument_;std::array<Voice,maxVoices> voices_{};
 MPEExpression mpe_;ModulationMatrix modulation_;std::size_t voiceLimit_=88;
 float lfoRateHz_=1.0f;Lfo::Shape lfoShape_=Lfo::Shape::Sine;
 double outputRate_=44100.0;
};
}
