#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace sampler {
struct SampleReference { std::string id,path; double sampleRate=44100.0; std::uint32_t channels=0; std::uint64_t frames=0; };
struct SampleZone { std::string id; SampleReference sample; std::uint8_t keyLow=0,keyHigh=127,velocityLow=1,velocityHigh=127,rootKey=60; float gainDb=0.0f,pan=0.0f; std::uint8_t roundRobinGroup=0,keyFadeIn=0,keyFadeOut=0,velocityFadeIn=0,velocityFadeOut=0; };
struct ZoneMatch { const SampleZone* zone=nullptr; float keyGain=1.0f,velocityGain=1.0f; };
class InstrumentModel { public: bool addZone(SampleZone,std::string* error=nullptr); bool replaceZone(const std::string&,SampleZone,std::string* error=nullptr); bool removeZone(const std::string&); void clear()noexcept; bool validate(std::string* error=nullptr)const; std::vector<ZoneMatch> select(std::uint8_t,std::uint8_t); const std::vector<SampleZone>& zones()const noexcept{return zones_;} std::size_t size()const noexcept{return zones_.size();} private: static bool valid(const SampleZone&,std::string*); static float fadeGain(int,int,int,int,int); std::vector<SampleZone> zones_; std::map<std::uint8_t,std::size_t> roundRobinCursor_; };
}
