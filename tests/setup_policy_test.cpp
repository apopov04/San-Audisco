#include "setup_policy.h"
#include <cassert>
#include <iostream>
#include <vector>

// A mock for the mutation boundary: use the production policy to choose actions,
// count actual requested installations, and never invoke platform installers.
struct Fixture {
    int invocations=0;
    bool ready=false,installed=false,restart=false;
    int run(){int action=ab_setup_action(ready,installed,restart);if(action==AB_INSTALL)++invocations;return action;}
};
int main(){
    Fixture compatible{0,true,true,false};assert(compatible.run()==AB_KEEP&&compatible.invocations==0);
    Fixture incompatible{0,false,true,false};assert(incompatible.run()==AB_REVIEW&&incompatible.invocations==0);
    Fixture absent;assert(absent.run()==AB_INSTALL&&absent.invocations==1);
    assert(ab_setup_installer_result(AB_CANCELLED)==AB_CANCELLED_RETRY);
    assert(ab_setup_installer_result(AB_FAILED)==AB_FAILED_RETRY);
    assert(ab_setup_installer_result(AB_RETURNED)==AB_VERIFY);
    // Returned without endpoints cannot mark the component ready.
    absent.installed=true;assert(absent.run()==AB_REVIEW);
    absent.restart=true;assert(absent.run()==AB_RESTART);
    // Reopening after restart must use newly observed readiness, not old state.
    absent.restart=false;absent.ready=true;assert(absent.run()==AB_KEEP);
    // A partial installation keeps its first component and installs only missing.
    std::vector<Fixture> partial{{0,true,true,false},{0,false,false,false}};
    for(auto& part:partial)part.run();assert(partial[0].invocations==0&&partial[1].invocations==1);
    assert(ab_setup_remove_shared_driver()==0);
    std::cout<<"PASS compatible skip, incompatible preservation, missing install, cancellation, failure, verification, restart resume, partial installation, shared-driver retention\n";
}
