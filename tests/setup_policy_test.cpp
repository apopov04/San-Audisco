#include "setup_policy.h"
#include <stdexcept>
#include <iostream>
#include <vector>

// Keep checks active in optimized builds; assert() is disabled by NDEBUG.
void require(bool condition) {
    if (!condition) throw std::runtime_error("Setup policy expectation failed");
}

// A mock for the mutation boundary: use the production policy to choose actions,
// count actual requested installations, and never invoke platform installers.
struct Fixture {
    int invocations=0;
    bool ready=false,installed=false,restart=false;
    int run(){int action=ab_setup_action(ready,installed,restart);if(action==AB_INSTALL)++invocations;return action;}
};
int main(){
    Fixture compatible{0,true,true,false};require(compatible.run()==AB_KEEP&&compatible.invocations==0);
    Fixture incompatible{0,false,true,false};require(incompatible.run()==AB_REVIEW&&incompatible.invocations==0);
    Fixture absent;require(absent.run()==AB_INSTALL&&absent.invocations==1);
    require(ab_setup_installer_result(AB_CANCELLED)==AB_CANCELLED_RETRY);
    require(ab_setup_installer_result(AB_FAILED)==AB_FAILED_RETRY);
    require(ab_setup_installer_result(AB_RETURNED)==AB_VERIFY);
    // Returned without endpoints cannot mark the component ready.
    absent.installed=true;require(absent.run()==AB_REVIEW);
    absent.restart=true;require(absent.run()==AB_RESTART);
    // Reopening after restart must use newly observed readiness, not old state.
    absent.restart=false;absent.ready=true;require(absent.run()==AB_KEEP);
    // A partial installation keeps its first component and installs only missing.
    std::vector<Fixture> partial{{0,true,true,false},{0,false,false,false}};
    for(auto& part:partial)part.run();require(partial[0].invocations==0&&partial[1].invocations==1);
    require(ab_setup_remove_shared_driver()==0);
    // Guided setup must never offer Open before all gates hold, or offer a
    // vendor installer over an existing component. Exhaust every boolean state.
    for(int bits=0;bits<128;++bits){
        const bool verified=bits&1,current=bits&2,a=bits&4,ap=bits&8,b=bits&16,bp=bits&32,restart=bits&64;
        const auto next=ab_setup_next(verified,current,a,ap,b,bp,restart);
        if(!verified)require(next==AB_CHECK);
        if(next==AB_OPEN)require(verified&&current&&a&&b&&!restart);
        if(next==AB_FIRST_COMPONENT)require(verified&&current&&!a&&!ap);
        if(next==AB_SECOND_COMPONENT)require(verified&&current&&!b&&!bp&&(a||ap));
        if(next==AB_NEEDS_REVIEW)require(verified&&current&&!restart&&(!a||!b)&&(a||ap)&&(b||bp));
        if(next==AB_NEEDS_RESTART)require(verified&&current&&restart&&(a||ap)&&(b||bp));
    }
    require(ab_setup_next(1,1,1,1,0,0,1)==AB_SECOND_COMPONENT); // Finish missing driver before reboot.
    require(ab_setup_next(1,1,0,1,1,1,0)==AB_NEEDS_REVIEW);
    require(ab_setup_next(1,0,1,1,1,1,0)==AB_APP); // Existing old app needs update.
    std::cout<<"PASS compatible skip, incompatible preservation, missing install, cancellation, failure, verification, restart resume, partial installation, shared-driver retention\n";
}
