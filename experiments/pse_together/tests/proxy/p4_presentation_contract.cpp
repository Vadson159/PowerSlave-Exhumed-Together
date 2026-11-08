#include "presentation_buffer.hpp"
#include "portal_plan.hpp"
#include "spawn_anchor.hpp"
#include "background_policy.hpp"
#include <cstdio>
#include <limits>

int checks=0,failures=0;
void check(bool ok,const char* name) { ++checks;if(!ok) { ++failures;std::printf("FAIL %s\n",name); } }
pse::proxy::ProxyCommand sample(unsigned seq,unsigned tick,float x=0) {
    pse::proxy::ProxyCommand c;c.sequence=seq;c.serverTick=tick;c.mapEpoch=7;c.connectionGeneration=4;c.entityId=21760;c.entityGeneration=1;c.x=x;return c;
}
pse::portal::Sector room(float xmin,float xmax,int left,int right) {
    return {{{{1,0,0,-xmin},left,left>=0},{{-1,0,0,xmax},right,right>=0},
             {{0,1,0,100},-1,false},{{0,-1,0,100},-1,false},{{0,0,1,100},-1,false},{{0,0,-1,100},-1,false}},0};
}
void clockScenario(int ppm,int pauseFrames,bool catchup) {
    pse::presentation::Buffer b;pse::proxy::ProxyCommand out;
    unsigned sequence=0,lastTick=0;bool monotonic=true,pushes=true,bounded=true;
    double previous=0,minLag=1e9,maxLag=0,maxJump=0;
    // 30 minutes on independently drifting clocks, 20 Hz snapshots, 60 Hz render.
    for(unsigned frame=0;frame<=108000;++frame) {
        const auto now=1000000000ULL+static_cast<std::uint64_t>(frame)*1000000000ULL/60;
        unsigned tick=static_cast<unsigned>(static_cast<double>(frame)/(1+ppm/1e6));
        const bool paused=frame>=600 && frame<600+static_cast<unsigned>(pauseFrames);
        if(!catchup && frame>=600+static_cast<unsigned>(pauseFrames)) tick-=static_cast<unsigned>(pauseFrames);
        if(!paused && (!sequence || tick>=lastTick+3)) {
            pushes &= b.push(sample(++sequence,tick,static_cast<float>(tick)*0.01f),now);lastTick=tick;
        }
        if(b.evaluate(now,out)) {
            monotonic &= b.cursorTick()+1e-8>=previous;
            maxJump=std::max(maxJump,b.cursorTick()-previous);previous=b.cursorTick();
            if(frame>7200) {
                const double lag=b.backlogTicks()*1000/60;
                minLag=std::min(minLag,lag);maxLag=std::max(maxLag,lag);
                bounded &= lag>=90 && lag<=190;
            }
        }
    }
    std::printf("CLOCK ppm=%d pause_frames=%d catchup=%d lag_ms=[%.3f,%.3f] max_jump_ticks=%.3f reanchors=%u\n",ppm,pauseFrames,catchup,minLag,maxLag,maxJump,b.reanchors());
    check(pushes,"clock fixture samples accepted");check(monotonic,"clock correction never rewinds presentation");
    check(bounded,"30 minute backlog remains within delay bounds");
    check(maxJump<=12+(catchup?pauseFrames:0),"cursor jump is bounded by the observed host discontinuity");
    if(pauseFrames>=15) check(b.reanchors()>0,"empty buffer reanchors after a stall");
}
int main() {
    using pse::presentation::Buffer;using pse::bounded::Vec3;using pse::placement::Box;
    Buffer b;pse::proxy::ProxyCommand out;
    check(!b.evaluate(0,out),"empty has no output");
    check(b.push(sample(1,0),1000000000),"first sample");
    check(b.push(sample(2,6,60),1110000000),"jittered receive");
    check(b.evaluate(1210000000,out)&&std::abs(out.x-30)<0.0001f,"160ms delayed interpolation independent of positive receive jitter");
    check(b.evaluate(1220000000,out)&&std::abs(out.x-36)<0.0001f,"playout advances without a new packet");
    check(!b.push(sample(2,6,99),1170000000),"duplicate rejection");
    check(!b.push(sample(1,4,20),1170000000),"old sequence rejection");
    check(!b.push(sample(3,6,20),1170000000),"non-advancing server clock rejection");
    check(!b.push(sample(3,8,20),1080000000),"backward receive clock");
    auto changed=sample(3,8);changed.entityGeneration++;
    check(!b.push(changed,1170000000),"old buffer cannot cross entity generation");
    changed=sample(3,8);changed.mapEpoch++;
    check(!b.push(changed,1170000000),"epoch requires explicit clear");
    changed=sample(3,8);changed.connectionGeneration++;
    check(!b.push(changed,1170000000),"connection requires explicit clear");
    changed=sample(3,8);changed.x=std::numeric_limits<float>::quiet_NaN();
    check(!b.push(changed,1170000000),"NaN refused");
    check(b.evaluate(1500000000,out)&&out.x==60,"loss freezes at real newest sample");
    check(b.evaluate(2500000000,out)&&out.x==60,"no extrapolation after long loss");
    b.clear();check(b.size()==0&&!b.evaluate(3000000000,out),"teardown removes every queued pose");
    auto a=sample(0xfffffffe,0xfffffffe,0),c=sample(1,4,60);a.yaw=3.10f;c.yaw=-3.10f;
    check(b.push(a,1000000000)&&b.push(c,1100000000),"sequence and tick wrap");
    check(b.evaluate(1210000000,out)&&std::abs(out.x-30)<0.001f,"wrap interpolates relative ticks");
    check(std::abs(out.yaw)>3.13f,"yaw follows short arc in radians");
    b.clear();check(b.push(sample(1,1,0),1000000000)&&b.push(sample(2,7,1000),1100000000),"teleport samples");
    check(b.evaluate(1210000000,out)&&out.x==0,"never interpolate through teleport wall");
    check(b.evaluate(1260000000,out)&&out.x==1000,"teleport becomes visible at its delayed time");
    b.clear();for(unsigned i=1;i<=200;++i) check(b.push(sample(i,i,static_cast<float>(i)),1000000000ULL+static_cast<std::uint64_t>(i)*16666667),"bounded producer");
    check(b.size()==64,"storage bounded under overrun");
    check(b.evaluate(5000000000,out)&&out.x==200,"overrun ends at latest real pose");
    check(!b.evaluate(4999999999,out),"backward playout clock refused");
    for(int ppm:{-500,-100,100,500}) clockScenario(ppm,0,false);
    for(int frames:{9,15,30,90}) { clockScenario(0,frames,false);clockScenario(0,frames,true); }
    // The actual run03 pause uncovered a large serverTick gap, not oscillator drift.
    b.clear();check(b.push(sample(1,100,0),1000000000)&&b.push(sample(2,103,1),1050000000),"long-pause setup");
    check(b.evaluate(2000000000,out)&&out.x==1,"stale pause holds newest real pose");
    check(b.push(sample(3,6103,2),101050000000ULL)&&b.evaluate(101050000000ULL,out)&&out.x==2&&b.discontinuities()==1,"100s host gap resumes at one actual sample without stale interpolation");
    check(b.push(sample(4,6106,3),101100000000ULL)&&b.evaluate(101100000000ULL,out)&&out.x==2,"discontinuity refills delay without extrapolation");
    for(unsigned i=2;i<=40;++i) { b.push(sample(i+3,6103+i*3,static_cast<float>(i+2)),101050000000ULL+static_cast<std::uint64_t>(i)*50000000);b.evaluate(101050000000ULL+static_cast<std::uint64_t>(i)*50000000,out); }
    check(b.backlogTicks()>=9&&b.backlogTicks()<=10,"normal delay restored within two seconds of a long pause");

    const Box anchor{{0,0,0},{-10,-10,0},{10,10,30}};
    pse::placement::WaitBudget wait;bool early=false;for(unsigned i=0;i<1799;++i)early|=wait.deferred();
    check(!early&&!wait.expired(),"occupied anchor can wait for bounded eligible ticks");
    check(wait.deferred()&&wait.expired()&&wait.deferred(),"permanently occupied anchor expires without allocation fallback");
    wait.clear();check(!wait.expired(),"world barrier clears anchor wait budget");
    check(!pse::placement::separate(anchor,anchor),"same native point is occupied");
    check(!pse::placement::separate(anchor,{{20,0,0},anchor.lo,anchor.hi}),"touching bounds refused");
    check(pse::placement::separate(anchor,{{24,0,0},anchor.lo,anchor.hi}),"clear anchor");
    auto bad=anchor;bad.hi[0]=-11;check(!pse::placement::separate(anchor,bad),"unknown bounds defer spawn");
    check(pse::placement::sameSupport(1152,1664,1152,1664),"native floor origin is not diagnostic AABB center");
    check(!pse::placement::sameSupport(1152,1664,1160,1664),"moved support invalidates cached anchor");
    check(!pse::placement::sameSupport(1152,1664,1152,1152),"closed support invalidates anchor");
    bad=anchor;bad.position[0]=std::numeric_limits<float>::infinity();check(!pse::placement::separate(anchor,bad),"nonfinite actor defers spawn");
    auto left=room(-200,0,-1,8),right=room(0,200,3,-1);
    check(pse::portal::corridor(left,3,right,8,Vec3{-30,0,0},Vec3{30,0,0}),"arbitrary sector IDs cross through aperture");
    check(pse::portal::sweptAperture(left,3,right,8,Vec3{-13,0,0},Vec3{-1,0,0}),"continuous approach can straddle portal plane");
    check(pse::portal::corridor(left,3,right,8,Vec3{-1,0,0},Vec3{1,0,0}),"small continuous crossing");
    check(!pse::portal::corridor(left,3,right,8,Vec3{-30,95,0},Vec3{30,95,0}),"wall-clipping AABB refused");
    auto broken=right;broken.faces[0].adjoin=99;check(!pse::portal::corridor(left,3,broken,8,Vec3{-30,0,0},Vec3{30,0,0}),"nonreciprocal portal refused");
    broken=right;broken.faces[0].plane[3]=1;check(!pse::portal::corridor(left,3,broken,8,Vec3{-30,0,0},Vec3{30,0,0}),"noncoplanar portal refused");
    broken=right;broken.flags=0x100;check(!pse::portal::corridor(left,3,broken,8,Vec3{-30,0,0},Vec3{30,0,0}),"unknown water transition refused");
    broken=right;broken.faces.push_back(broken.faces[0]);check(!pse::portal::corridor(left,3,broken,8,Vec3{-30,0,0},Vec3{30,0,0}),"ambiguous portal refused");
    check(!pse::portal::corridor(left,3,right,8,Vec3{-30,0,0},Vec3{300,0,0}),"destination outside geometry refused");

    background::Conditions policy{true,true,true,true,true,false,false,false,true,true};
    check(background::eligible(policy),"ready background policy");policy.ready=false;check(!background::eligible(policy),"transport alone never grants lease");
    policy.ready=true;policy.stopping=true;check(!background::eligible(policy),"shutdown denies lease");
    background::FocusLease lease;unsigned char focus=1;
    check(lease.acquire(focus,1,true)&&focus==0,"lease clears only focus gate");
    check(!lease.acquire(focus,1,true),"nested lease refused");
    focus=0;check(!lease.release(focus,2)&&focus==0,"new native focus gain wins");
    std::printf("P4 CONTRACT checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
