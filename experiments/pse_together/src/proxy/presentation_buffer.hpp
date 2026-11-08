// P4: simulation-owned bounded playout with a rolling receive/server clock offset.
// No absolute cross-process clock subtraction, extrapolation, allocation, or engine access.
#pragma once
#include "visual_proxy.hpp"
#include "bounded_proxy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace pse::presentation {
class Buffer {
    struct Sample { proxy::ProxyCommand command; double tick; };
    struct ClockSample { std::uint64_t received; double offset; };
    std::array<Sample,64> samples_{};
    std::array<ClockSample,256> clock_{};
    std::size_t count_{};
    std::size_t clockCount_{},clockNext_{};
    std::uint64_t originNs_{},receivedNs_{},evaluatedNs_{};
    double cursor_{},offset_{};
    unsigned reanchors_{},discontinuities_{};
    bool evaluated_{},starved_{};
    static constexpr std::uint64_t WindowNs=2000000000;
    double localTick(std::uint64_t now) const { return static_cast<double>(now-originNs_)*60.0/1e9; }
    double minimumOffset(std::uint64_t now) const {
        double result=clock_[(clockNext_+clock_.size()-1)%clock_.size()].offset;
        for(std::size_t i=0;i<clockCount_;++i)
            if(now-clock_[i].received<=WindowNs) result=std::min(result,clock_[i].offset);
        return result;
    }
    static bool sameIdentity(const proxy::ProxyCommand& a,const proxy::ProxyCommand& b) {
        return a.abi==b.abi && a.slot==b.slot && a.mapEpoch==b.mapEpoch &&
            a.connectionGeneration==b.connectionGeneration && a.entityId==b.entityId && a.entityGeneration==b.entityGeneration;
    }
public:
    // DOC-67 measured a 123 ms host-clock deficit. 160 ms includes two 60 Hz steps.
    static constexpr std::uint64_t DelayNs=160000000, StaleNs=250000000;
    void clear() noexcept {
        count_=clockCount_=clockNext_=0;originNs_=receivedNs_=evaluatedNs_=0;
        cursor_=offset_=0;reanchors_=discontinuities_=0;evaluated_=starved_=false;
    }
    std::size_t size() const noexcept { return count_; }
    double cursorTick() const noexcept { return cursor_; }
    double backlogTicks() const noexcept { return count_ ? samples_[count_-1].tick-cursor_ : 0; }
    unsigned reanchors() const noexcept { return reanchors_; }
    unsigned discontinuities() const noexcept { return discontinuities_; }
    bool push(const proxy::ProxyCommand& c,std::uint64_t now) noexcept {
        if(!bounded::finite({c.x,c.y,c.z}) || !std::isfinite(c.yaw) || !std::isfinite(c.pitch)) return false;
        double tick=0;
        if(count_) {
            const auto& last=samples_[count_-1];
            const auto seq=c.sequence-last.command.sequence,dt=c.serverTick-last.command.serverTick;
            if(now<receivedNs_ || (evaluated_ && now<evaluatedNs_) || !sameIdentity(last.command,c) || !seq || seq>=0x80000000u ||
                !dt || dt>=0x80000000u) return false;
            tick=last.tick+dt;
        } else originNs_=now;
        if(count_==samples_.size()) { std::move(samples_.begin()+1,samples_.end(),samples_.begin());--count_; }
        const double observation=localTick(now)-tick;
        // Once playback has run dry, refill in place; never rewind the visible pose.
        // Discard the pre-stall clock minimum: otherwise a host that dropped ticks
        // would keep rendering each incoming packet immediately for the whole window.
        if(starved_) {
            clockCount_=clockNext_=0;offset_=observation;starved_=false;++reanchors_;
            // A long host pause can advance serverTick while publishing no poses.
            // Do not spend minutes interpolating that unsampled interval. Resume
            // at the first real pose, hold it while the normal delay refills.
            if(count_ && tick-samples_[count_-1].tick>32) {
                count_=0;cursor_=tick;++discontinuities_;
            }
        }
        clock_[clockNext_]={now,observation};clockNext_=(clockNext_+1)%clock_.size();
        clockCount_=std::min(clockCount_+1,clock_.size());
        samples_[count_++]={c,tick};receivedNs_=now;return true;
    }
    bool evaluate(std::uint64_t now,proxy::ProxyCommand& out) noexcept {
        if(!count_ || now<receivedNs_ || (evaluated_ && now<evaluatedNs_)) return false;
        const double minimum=minimumOffset(now);
        // Track oscillator drift and tick deficits smoothly (at most 0.5% speed
        // correction). The cursor remains monotonic even while replenishing.
        const double budget=evaluated_ ? static_cast<double>(now-evaluatedNs_)*60.0/1e9*0.005 : 0;
        if(!evaluated_) offset_=minimum;
        else offset_+=std::clamp(minimum-offset_,-budget,budget);
        double desired=std::max(0.0,localTick(now)-offset_-static_cast<double>(DelayNs)*60.0/1e9);
        const double newest=samples_[count_-1].tick;
        if(now-receivedNs_>StaleNs) { desired=newest;starved_=true; }
        else {
            if(desired>=newest && newest>0) starved_=true;
            // A host catchup burst must not jump the cursor to the newest tick.
            if(evaluated_) desired=std::min(desired,cursor_+static_cast<double>(now-evaluatedNs_)*60.0/1e9*1.1);
        }
        cursor_=evaluated_ ? std::max(cursor_,desired) : desired;evaluated_=true;
        evaluatedNs_=now;cursor_=std::clamp(cursor_,samples_[0].tick,newest);
        if(cursor_<=samples_[0].tick) { out=samples_[0].command;return true; }
        for(std::size_t i=1;i<count_;++i) if(cursor_<=samples_[i].tick) {
            const auto& a=samples_[i-1];const auto& b=samples_[i];
            if(bounded::teleport({a.command.x,a.command.y,a.command.z},{b.command.x,b.command.y,b.command.z})) {
                out=cursor_<b.tick ? a.command : b.command;return true;
            }
            const double alpha=(cursor_-a.tick)/(b.tick-a.tick);out=b.command;
            const auto lerp=[&](float x,float y) { return static_cast<float>(x+(static_cast<double>(y)-x)*alpha); };
            out.x=lerp(a.command.x,b.command.x);out.y=lerp(a.command.y,b.command.y);out.z=lerp(a.command.z,b.command.z);
            out.pitch=lerp(a.command.pitch,b.command.pitch);
            out.yaw=bounded::normalizeYaw(static_cast<float>(a.command.yaw+bounded::normalizeYaw(b.command.yaw-a.command.yaw)*alpha));
            return true;
        }
        out=samples_[count_-1].command;return true;
    }
};
}
