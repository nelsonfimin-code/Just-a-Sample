#include "MultisampleEngine.h"
#include <algorithm>
#include <cmath>
namespace sampler {
namespace { constexpr float pi=3.14159265358979323846f; }
bool MultisampleEngine::setInstrument(const InstrumentModel& model,const ZoneAudio* audio,std::size_t count){
 if(count>maxZones||model.size()>maxZones||model.size()!=count||(count&&audio==nullptr))return false;
 std::string error;if(!model.validate(&error))return false;
 InstrumentModel candidate=model;
 std::array<ZoneAudio,maxZones> prepared{};
 std::size_t preparedCount=0;
 for(const auto& zone:candidate.zones()){
  auto it=std::find_if(audio,audio+count,[&](const ZoneAudio& a){return a.zone&&a.zone->id==zone.id;});
  if(it==audio+count||!it->left||it->frames==0||!std::isfinite(it->sampleRate)||it->sampleRate<=0)return false;
  prepared[preparedCount]=*it;prepared[preparedCount].zone=&zone;++preparedCount;
 }
 reset();instrument_=std::move(candidate);audio_=prepared;audioCount_=preparedCount;return true;
}
void MultisampleEngine::reset()noexcept{for(auto& v:voices_)v={};mpe_.reset();}
void MultisampleEngine::setOutputSampleRate(double rate)noexcept{if(std::isfinite(rate)&&rate>0.0)outputRate_=rate;}
void MultisampleEngine::setVoiceLimit(std::size_t voices)noexcept{voiceLimit_=std::clamp<std::size_t>(voices,1,maxVoices);for(std::size_t i=voiceLimit_;i<maxVoices;++i)voices_[i].active=false;}
void MultisampleEngine::setMPEZoneConfig(MPEZoneConfig config)noexcept{mpe_.configure(config);refreshExpression(0);}
MultisampleEngine::Voice* MultisampleEngine::allocateVoice()noexcept{
 for(std::size_t i=0;i<voiceLimit_;++i)if(!voices_[i].active)return &voices_[i];
 auto it=std::min_element(voices_.begin(),voices_.begin()+std::ptrdiff_t(voiceLimit_),[](const Voice&a,const Voice&b){return a.releaseGain<b.releaseGain;});
 return it==voices_.end()?nullptr:&*it;
}
ModulationInputs MultisampleEngine::inputsFor(const Voice& v,float lfoValue)const noexcept{
 auto inputs=mpe_.modulationInputs(v.channel,v.note,v.velocity,v.keyTrack,lfoValue,v.releaseGain);
 inputs.polyPressure=v.pressure;inputs.slide=v.slide;return inputs;
}
void MultisampleEngine::noteOn(std::uint8_t ch,std::uint8_t note,std::uint8_t velocity)noexcept{
 if(ch<1||ch>16||velocity==0)return;
 auto matches=instrument_.select(note,velocity);
 const auto expression=mpe_.forChannel(ch);
 for(const auto& match:matches){
  if(!match.zone)continue;
  auto a=std::find_if(audio_.begin(),audio_.begin()+std::ptrdiff_t(audioCount_),[&](const ZoneAudio& x){return x.zone==match.zone;});
  if(a==audio_.begin()+std::ptrdiff_t(audioCount_))continue;
  auto* v=allocateVoice();if(!v)continue;
  v->audio=&*a;v->channel=ch;v->note=note;v->velocity=float(velocity)/127.0f;
  v->keyTrack=std::clamp((float(note)-60.0f)/60.0f,-1.0f,1.0f);
  v->step=std::pow(2.0,double(float(int(note)-int(match.zone->rootKey))+expression.pitchBendSemitones)/12.0)*a->sampleRate/(outputRate_>0?outputRate_:a->sampleRate);
  v->gain=match.velocityGain*match.keyGain*v->velocity*v->velocity;
  v->zoneGainDb=match.zone->gainDb;v->pan=match.zone->pan;v->pressure=std::max(expression.pressure,mpe_.perNotePressure(ch,note));v->slide=expression.slide;
  v->lfo.configure(lfoRateHz_,lfoShape_);v->lfo.reset();
  auto inputs=inputsFor(*v,0.0f);
  const auto start=modulation_.evaluate(ModDestination::SampleStart,0.0f,0.0f,1.0f,inputs);
  v->position=double(start)*double(a->frames>0?a->frames-1:0);
  const auto attackMs=modulation_.evaluate(ModDestination::AttackMs,0.0f,0.0f,5000.0f,inputs);
  v->attackGain=attackMs>0.0f?0.0f:1.0f;
  v->attackStep=attackMs>0.0f?1.0f/float((outputRate_>0?outputRate_:44100.0)*double(attackMs)/1000.0):0.0f;
  v->filterL=v->filterR=0;v->releaseGain=1;v->releaseStep=0;
  v->active=true;v->releasing=false;v->keyDown=true;
 }
}
void MultisampleEngine::noteOff(std::uint8_t ch,std::uint8_t note)noexcept{
 const auto expression=mpe_.forChannel(ch);
 for(auto& v:voices_)if(v.active&&v.channel==ch&&v.note==note){
  v.keyDown=false;
  if(expression.sustain<=0.0f){
   const auto inputs=inputsFor(v,v.lfo.value());
   const auto releaseMs=modulation_.evaluate(ModDestination::ReleaseMs,80.0f,1.0f,5000.0f,inputs);
   v.releasing=true;v.releaseStep=1.0f/float((outputRate_>0?outputRate_:44100.0)*double(releaseMs)/1000.0);
  }
 }
}
void MultisampleEngine::refreshExpression(std::uint8_t ch)noexcept{
 for(auto& v:voices_)if(v.active&&(ch==0||v.channel==ch||mpe_.isMemberChannel(v.channel))){
  const auto e=mpe_.forChannel(v.channel);
  v.pressure=std::max(e.pressure,mpe_.perNotePressure(v.channel,v.note));v.slide=e.slide;
  const auto semitones=float(int(v.note)-int(v.audio->zone->rootKey))+e.pitchBendSemitones;
  v.step=std::pow(2.0,double(semitones)/12.0)*v.audio->sampleRate/(outputRate_>0?outputRate_:v.audio->sampleRate);
 }
}
void MultisampleEngine::handleMidi(const EngineMidiEvent& e)noexcept{
 if(e.channel<1||e.channel>16)return;
 switch(e.type){
  case EngineMidiEvent::Type::NoteOn:if(e.value>0)noteOn(e.channel,e.note,std::uint8_t(std::min<std::uint16_t>(e.value,127)));else noteOff(e.channel,e.note);return;
  case EngineMidiEvent::Type::NoteOff:noteOff(e.channel,e.note);return;
  case EngineMidiEvent::Type::PitchBend:mpe_.process({MPEEvent::Type::PitchBend,e.channel,e.note,0,e.value});refreshExpression(e.channel);return;
  case EngineMidiEvent::Type::ChannelPressure:mpe_.process({MPEEvent::Type::ChannelPressure,e.channel,e.note,0,e.value});refreshExpression(e.channel);return;
  case EngineMidiEvent::Type::PolyPressure:mpe_.process({MPEEvent::Type::PolyPressure,e.channel,e.note,0,e.value});refreshExpression(e.channel);return;
  case EngineMidiEvent::Type::ControlChange:
   mpe_.process({MPEEvent::Type::ControlChange,e.channel,e.note,e.controller,e.value});
   if(e.controller==64&&e.value<64)for(auto& v:voices_)if(v.active&&!v.keyDown&&(v.channel==e.channel||mpe_.isMemberChannel(v.channel))&&mpe_.forChannel(v.channel).sustain<=0.0f){
    const auto inputs=inputsFor(v,v.lfo.value());const auto releaseMs=modulation_.evaluate(ModDestination::ReleaseMs,80.0f,1.0f,5000.0f,inputs);
    v.releasing=true;v.releaseStep=1.0f/float((outputRate_>0?outputRate_:44100.0)*double(releaseMs)/1000.0);
   }
   if(e.controller==1||e.controller==74||e.controller==64)refreshExpression(e.channel);
   return;
 }
}
void MultisampleEngine::render(float* left,float* right,std::size_t samples,double rate)noexcept{
 if(!left||!std::isfinite(rate)||rate<=0)return;outputRate_=rate;
 for(auto& v:voices_){
  if(!v.active||!v.audio)continue;
  const auto& a=*v.audio;const auto& z=*a.zone;
  const auto totalFrames=std::min<std::uint64_t>(a.frames,z.sample.frames);
  if(totalFrames==0){v.active=false;continue;}
  for(std::size_t i=0;i<samples;++i){
   if(v.position>=double(totalFrames)){v.active=false;break;}
   const auto inputs=inputsFor(v,v.lfo.next(float(rate)));
   const auto pitchMod=modulation_.evaluate(ModDestination::PitchSemitones,0.0f,-96.0f,96.0f,inputs);
   const auto gainDb=modulation_.evaluate(ModDestination::GainDb,v.zoneGainDb,-96.0f,24.0f,inputs);
   const auto pan=modulation_.evaluate(ModDestination::Pan,v.pan,-1.0f,1.0f,inputs);
   const auto cutoffBase=float(rate*0.49);
   const auto cutoff=modulation_.evaluate(ModDestination::FilterCutoffHz,cutoffBase,20.0f,float(rate*0.49),inputs);
   const auto filterEnabled=cutoff<float(rate*0.48);
   const auto alpha=filterEnabled?1.0f-std::exp(-2.0f*pi*cutoff/float(rate)):1.0f;
   const auto idx=std::uint64_t(v.position);const auto next=std::min<std::uint64_t>(idx+1,totalFrames-1);const auto frac=float(v.position-double(idx));
   const auto l0=a.left[idx],l1=a.left[next];const auto rawL=l0+(l1-l0)*frac;
   const auto* rp=a.right?a.right:a.left;const auto r0=rp[idx],r1=rp[next];const auto rawR=r0+(r1-r0)*frac;
   v.filterL+=alpha*(rawL-v.filterL);v.filterR+=alpha*(rawR-v.filterR);
   const auto lGain=std::cos((pan+1.0f)*pi*0.25f);const auto rGain=std::sin((pan+1.0f)*pi*0.25f);
   const auto pressureGain=1.0f+0.1f*v.pressure;
   const auto gain=v.gain*std::pow(10.0f,gainDb/20.0f)*pressureGain*v.attackGain*v.releaseGain;
   left[i]+=v.filterL*gain*lGain;if(right)right[i]+=v.filterR*gain*rGain;
   v.position+=v.step*std::exp2(pitchMod/12.0f);
   if(v.attackGain<1.0f)v.attackGain=std::min(1.0f,v.attackGain+v.attackStep);
   if(v.releasing){v.releaseGain=std::max(0.0f,v.releaseGain-v.releaseStep);if(v.releaseGain<=0.0f){v.active=false;break;}}
  }
 }
}
std::size_t MultisampleEngine::activeVoices()const noexcept{return std::size_t(std::count_if(voices_.begin(),voices_.end(),[](const Voice&v){return v.active;}));}
}
