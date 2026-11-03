// Execute the original suite, unchanged assertions/deadlines, in this process
// with a bounded 1ms Windows timer request. A diagnostic environment comparison.
#define main original_suite_main
#include "../../../tools/enet-prototype/tests/test_suite.cpp"
#undef main
#include <windows.h>
#include <mmsystem.h>
int main(int argc,char** argv) {
    const auto result=timeBeginPeriod(1);
    std::printf("TIMER_FIXTURE timeBeginPeriod(1)=%u; original assertions/deadlines unchanged\n",result);
    const int code=original_suite_main(argc,argv);
    if(result==TIMERR_NOERROR)timeEndPeriod(1);
    return code;
}
