#include "InstrumentModel.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <utility>
namespace sampler {
namespace {
constexpr std::uint32_t maxZones = 4096, maxString = 1024 * 1024;
void putU32(std::vector<std::uint8_t>& b,std::uint32_t v){for(int i=0;i<4;++i)b.push_back(std::uint8_t(v>>(i*8)));}
void putU64(std::vector<std::uint8_t>& b,std::uint64_t v){for(int i=0;i<8;++i)b.push_back(std::uint8_t(v>>(i*8)));}
void putFloat(std::vector<std::uint8_t>& b,float v){std::uint32_t x;std::memcpy(&x,&v,sizeof(x));putU32(b,x);}
void putDouble(std::vector<std::uint8_t>& b,double v){std::uint64_t x;std::memcpy(&x,&v,sizeof(x));putU64(b,x);}
bool putString(std::vector<std::uint8_t>& b,const std::string& s){if(s.size()>maxString)return false;putU32(b,std::uint32_t(s.size()));b.insert(b.end(),s.begin(),s.end());return true;}
class Reader{
public:
 explicit Reader(std::span<const std::uint8_t> bytes):b(bytes){}
 bool u8(std::uint8_t& v){if(pos>=b.size())return false;v=b[pos++];return true;}
 bool u32(std::uint32_t& v){if(b.size()-pos<4)return false;v=0;for(int i=0;i<4;++i)v|=std::uint32_t(b[pos++])<<(i*8);return true;}
 bool u64(std::uint64_t& v){if(b.size()-pos<8)return false;v=0;for(int i=0;i<8;++i)v|=std::uint64_t(b[pos++])<<(i*8);return true;}
 bool f32(float& v){std::uint32_t x;if(!u32(x))return false;std::memcpy(&v,&x,sizeof(v));return true;}
 bool f64(double& v){std::uint64_t x;if(!u64(x))return false;std::memcpy(&v,&x,sizeof(v));return true;}
 bool str(std::string& s){std::uint32_t n;if(!u32(n)||n>maxString||b.size()-pos<n)return false;s.assign(reinterpret_cast<const char*>(b.data()+pos),n);pos+=n;return true;}
 bool done()const{return pos==b.size();}
private:std::span<const std::uint8_t>b;std::size_t pos=0;
};
}
bool InstrumentModel::valid(const SampleZone& z,std::string* e){
 auto fail=[&](const char* m){if(e)*e=m;return false;};
 if(z.id.empty())return fail("Zone id is empty.");
 if(z.sample.id.empty()||z.sample.path.empty())return fail("Sample reference is incomplete.");
 if(!std::isfinite(z.sample.sampleRate)||z.sample.sampleRate<=0)return fail("Invalid sample rate.");
 if(!z.sample.channels||!z.sample.frames)return fail("Sample has no audio frames or channels.");
 if(z.keyLow>z.keyHigh||z.velocityLow==0||z.velocityLow>z.velocityHigh)return fail("Invalid key or velocity range.");
 if(z.rootKey<z.keyLow||z.rootKey>z.keyHigh)return fail("Root key must be inside key range.");
 if(!std::isfinite(z.gainDb)||z.gainDb< -96||z.gainDb>24)return fail("Gain must be -96 to +24 dB.");
 if(!std::isfinite(z.pan)||z.pan< -1||z.pan>1)return fail("Pan must be -1 to +1.");
 if(z.filterMode!=ZoneFilterMode::Off&&z.filterMode!=ZoneFilterMode::LowPass
    &&z.filterMode!=ZoneFilterMode::HighPass&&z.filterMode!=ZoneFilterMode::BandPass)
  return fail("Invalid zone filter mode.");
 if(!std::isfinite(z.filterCutoffHz)||z.filterCutoffHz<20.0f||z.filterCutoffHz>200000.0f)
  return fail("Filter cutoff must be between 20 Hz and 200 kHz.");
 if(!std::isfinite(z.driveDb)||z.driveDb<0.0f||z.driveDb>24.0f)
  return fail("Drive must be between 0 and 24 dB.");
 const auto end=z.sampleEnd==0?z.sample.frames:z.sampleEnd;
 if(z.sampleStart>=z.sample.frames||end<=z.sampleStart||end>z.sample.frames)return fail("Invalid sample start/end range.");
 if(z.loopMode!=ZoneLoopMode::Off){
  if(z.loopMode!=ZoneLoopMode::Forward&&z.loopMode!=ZoneLoopMode::PingPong)return fail("Invalid loop mode.");
  if(z.loopStart<z.sampleStart||z.loopEnd<=z.loopStart||z.loopEnd>end)return fail("Loop range must be inside the playback range.");
  if(z.loopCrossfadeFrames>(z.loopEnd-z.loopStart)/2)return fail("Loop crossfade cannot exceed half the loop length.");
 }else if(z.loopCrossfadeFrames!=0)return fail("Loop crossfade requires an enabled loop.");
 return true;
}
bool InstrumentModel::addZone(SampleZone z,std::string* e){if(!valid(z,e))return false;if(zones_.size()>=maxZones){if(e)*e="Instrument zone limit reached.";return false;}if(std::any_of(zones_.begin(),zones_.end(),[&](const auto& x){return x.id==z.id;})){if(e)*e="Duplicate zone id.";return false;}zones_.push_back(std::move(z));return true;}
bool InstrumentModel::replaceZone(const std::string& id,SampleZone z,std::string* e){if(!valid(z,e))return false;auto it=std::find_if(zones_.begin(),zones_.end(),[&](const auto& x){return x.id==id;});if(it==zones_.end()){if(e)*e="Zone not found.";return false;}if(z.id!=id&&std::any_of(zones_.begin(),zones_.end(),[&](const auto& x){return x.id==z.id;})){if(e)*e="Replacement id already exists.";return false;}*it=std::move(z);return true;}
bool InstrumentModel::removeZone(const std::string& id){auto n=zones_.size();std::erase_if(zones_,[&](const auto& z){return z.id==id;});return zones_.size()!=n;}
void InstrumentModel::clear()noexcept{zones_.clear();roundRobinCursor_={};}
float InstrumentModel::fadeGain(int v,int lo,int hi,int in,int out){float g=1.0f;if(in>0&&v<lo+in)g*=std::clamp(float(v-lo+1)/float(in+1),0.0f,1.0f);if(out>0&&v>hi-out)g*=std::clamp(float(hi-v+1)/float(out+1),0.0f,1.0f);return g;}
ZoneSelection InstrumentModel::select(std::uint8_t note,std::uint8_t vel)noexcept{
 ZoneSelection out;
 for(const auto& z:zones_)if(z.roundRobinGroup==0&&note>=z.keyLow&&note<=z.keyHigh&&vel>=z.velocityLow&&vel<=z.velocityHigh)
  out.push({&z,fadeGain(note,z.keyLow,z.keyHigh,z.keyFadeIn,z.keyFadeOut),fadeGain(vel,z.velocityLow,z.velocityHigh,z.velocityFadeIn,z.velocityFadeOut)});
 for(unsigned group=1;group<256;++group){std::size_t count=0;for(const auto& z:zones_)if(z.roundRobinGroup==group&&note>=z.keyLow&&note<=z.keyHigh&&vel>=z.velocityLow&&vel<=z.velocityHigh)++count;if(!count)continue;
  const auto target=roundRobinCursor_[group]%count;std::size_t seen=0;for(const auto& z:zones_)if(z.roundRobinGroup==group&&note>=z.keyLow&&note<=z.keyHigh&&vel>=z.velocityLow&&vel<=z.velocityHigh){if(seen++==target){out.push({&z,fadeGain(note,z.keyLow,z.keyHigh,z.keyFadeIn,z.keyFadeOut),fadeGain(vel,z.velocityLow,z.velocityHigh,z.velocityFadeIn,z.velocityFadeOut)});break;}}
  roundRobinCursor_[group]=(roundRobinCursor_[group]+1)%count;
 }
 return out;
}
bool InstrumentModel::validate(std::string* e)const{std::set<std::string> ids;for(const auto& z:zones_){if(!valid(z,e))return false;if(!ids.insert(z.id).second){if(e)*e="Duplicate zone id.";return false;}}return true;}
std::vector<std::uint8_t> InstrumentModel::serialize(std::string* e)const{
 if(!validate(e)||zones_.size()>maxZones)return {};
 std::vector<std::uint8_t>b;b.reserve(16+zones_.size()*96);b.insert(b.end(),{'J','A','S','Z'});putU32(b,3);putU32(b,std::uint32_t(zones_.size()));
 for(const auto& z:zones_){if(!putString(b,z.id)||!putString(b,z.sample.id)||!putString(b,z.sample.path)){if(e)*e="Instrument string exceeds storage limit.";return {};}
  putDouble(b,z.sample.sampleRate);putU32(b,z.sample.channels);putU64(b,z.sample.frames);
  b.insert(b.end(),{z.keyLow,z.keyHigh,z.velocityLow,z.velocityHigh,z.rootKey});
  putFloat(b,z.gainDb);putFloat(b,z.pan);b.insert(b.end(),{z.roundRobinGroup,z.keyFadeIn,z.keyFadeOut,z.velocityFadeIn,z.velocityFadeOut});
  putU64(b,z.sampleStart);putU64(b,z.sampleEnd);putU64(b,z.loopStart);putU64(b,z.loopEnd);putU32(b,z.loopCrossfadeFrames);b.push_back(std::uint8_t(z.loopMode));
  b.push_back(std::uint8_t(z.filterMode));putFloat(b,z.filterCutoffHz);putFloat(b,z.driveDb);
 }
 return b;
}
bool InstrumentModel::deserialize(std::span<const std::uint8_t> bytes,std::string* e){
 auto fail=[&](const char* m){if(e)*e=m;return false;};
 if(bytes.size()<12||std::memcmp(bytes.data(),"JASZ",4)!=0)return fail("Invalid instrument file signature.");
 Reader r(bytes.subspan(4));std::uint32_t version=0,count=0;if(!r.u32(version)||(version!=1&&version!=2&&version!=3))return fail("Unsupported instrument file version.");
 if(!r.u32(count)||count>maxZones)return fail("Invalid instrument zone count.");
 InstrumentModel candidate;
 for(std::uint32_t i=0;i<count;++i){SampleZone z;
  if(!r.str(z.id)||!r.str(z.sample.id)||!r.str(z.sample.path)||!r.f64(z.sample.sampleRate)||!r.u32(z.sample.channels)||!r.u64(z.sample.frames))return fail("Truncated instrument sample reference.");
  if(!r.u8(z.keyLow)||!r.u8(z.keyHigh)||!r.u8(z.velocityLow)||!r.u8(z.velocityHigh)||!r.u8(z.rootKey)||!r.f32(z.gainDb)||!r.f32(z.pan)||!r.u8(z.roundRobinGroup)||!r.u8(z.keyFadeIn)||!r.u8(z.keyFadeOut)||!r.u8(z.velocityFadeIn)||!r.u8(z.velocityFadeOut))return fail("Truncated instrument zone.");
  if(version>=2){
   std::uint8_t loopMode=0;
   if(!r.u64(z.sampleStart)||!r.u64(z.sampleEnd)||!r.u64(z.loopStart)||!r.u64(z.loopEnd)||!r.u32(z.loopCrossfadeFrames)||!r.u8(loopMode))return fail("Truncated sample playback settings.");
   z.loopMode=ZoneLoopMode(loopMode);
  }
  if(version>=3)
  {
   std::uint8_t filterMode=0;
   if(!r.u8(filterMode)||!r.f32(z.filterCutoffHz)||!r.f32(z.driveDb))
    return fail("Truncated zone effects settings.");
   z.filterMode=ZoneFilterMode(filterMode);
  }
  if(!candidate.addZone(std::move(z),e))return false;
 }
 if(!r.done())return fail("Unexpected trailing instrument data.");
 zones_=std::move(candidate.zones_);roundRobinCursor_={};return true;
}
}
