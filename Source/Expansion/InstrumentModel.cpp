#include "InstrumentModel.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
namespace sampler {
bool InstrumentModel::valid(const SampleZone& z,std::string* e){auto fail=[&](const char* m){if(e)*e=m;return false;};if(z.id.empty())return fail("Zone id is empty.");if(z.sample.id.empty()||z.sample.path.empty())return fail("Sample reference is incomplete.");if(!std::isfinite(z.sample.sampleRate)||z.sample.sampleRate<=0)return fail("Invalid sample rate.");if(!z.sample.channels||!z.sample.frames)return fail("Sample has no audio frames or channels.");if(z.keyLow>z.keyHigh||z.velocityLow==0||z.velocityLow>z.velocityHigh)return fail("Invalid key or velocity range.");if(z.rootKey<z.keyLow||z.rootKey>z.keyHigh)return fail("Root key must be inside key range.");if(!std::isfinite(z.gainDb)||z.gainDb< -96||z.gainDb>24)return fail("Gain must be -96 to +24 dB.");if(!std::isfinite(z.pan)||z.pan< -1||z.pan>1)return fail("Pan must be -1 to +1.");return true;}
bool InstrumentModel::addZone(SampleZone z,std::string* e){if(!valid(z,e))return false;if(std::any_of(zones_.begin(),zones_.end(),[&](const auto& x){return x.id==z.id;})){if(e)*e="Duplicate zone id.";return false;}zones_.push_back(std::move(z));return true;}
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
}
