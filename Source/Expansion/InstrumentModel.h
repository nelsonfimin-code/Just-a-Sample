#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace sampler {
struct SampleReference { std::string id,path; double sampleRate=44100.0; std::uint32_t channels=0; std::uint64_t frames=0; };
enum class ZoneLoopMode : std::uint8_t { Off, Forward, PingPong };
struct SampleZone {
 std::string id;
 SampleReference sample;
 std::uint8_t keyLow=0,keyHigh=127,velocityLow=1,velocityHigh=127,rootKey=60;
 float gainDb=0.0f,pan=0.0f;
 std::uint8_t roundRobinGroup=0,keyFadeIn=0,keyFadeOut=0,velocityFadeIn=0,velocityFadeOut=0;
 std::uint64_t sampleStart=0,sampleEnd=0,loopStart=0,loopEnd=0;
 std::uint32_t loopCrossfadeFrames=0;
 ZoneLoopMode loopMode=ZoneLoopMode::Off;
};
struct ZoneMatch { const SampleZone* zone=nullptr; float keyGain=1.0f,velocityGain=1.0f; };
class ZoneSelection {
public:
 static constexpr std::size_t capacity=128;
 bool push(ZoneMatch m)noexcept{if(count_==capacity){overflowed_=true;return false;}matches_[count_++]=m;return true;}
 bool overflowed()const noexcept{return overflowed_;}
 std::size_t size()const noexcept{return count_;}
 bool empty()const noexcept{return count_==0;}
 const ZoneMatch& operator[](std::size_t i)const noexcept{return matches_[i];}
 ZoneMatch* begin()noexcept{return matches_.data();} ZoneMatch* end()noexcept{return matches_.data()+count_;}
 const ZoneMatch* begin()const noexcept{return matches_.data();} const ZoneMatch* end()const noexcept{return matches_.data()+count_;}
private: std::array<ZoneMatch,capacity> matches_{};std::size_t count_=0;bool overflowed_=false;
};
class InstrumentModel {
public:
 bool addZone(SampleZone,std::string* error=nullptr); bool replaceZone(const std::string&,SampleZone,std::string* error=nullptr);
 bool removeZone(const std::string&); void clear()noexcept; bool validate(std::string* error=nullptr)const;
 ZoneSelection select(std::uint8_t note,std::uint8_t velocity)noexcept;
 std::vector<std::uint8_t> serialize(std::string* error=nullptr)const;
 bool deserialize(std::span<const std::uint8_t>,std::string* error=nullptr);
 std::vector<SampleZone>& zones()noexcept{return zones_;} const std::vector<SampleZone>& zones()const noexcept{return zones_;} std::size_t size()const noexcept{return zones_.size();}
private:
 static bool valid(const SampleZone&,std::string*); static float fadeGain(int,int,int,int,int);
 std::vector<SampleZone> zones_;std::array<std::size_t,256> roundRobinCursor_{};
};
}
