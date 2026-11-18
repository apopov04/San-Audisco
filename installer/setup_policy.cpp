#include "setup_policy.h"
int ab_setup_next(int verified,int app_current,int first_ready,int first_present,
                  int second_ready,int second_present,int restart_pending) {
    if(!verified)return AB_CHECK;
    if(!app_current)return AB_APP;
    if(!first_ready&&!first_present)return AB_FIRST_COMPONENT;
    if(!second_ready&&!second_present)return AB_SECOND_COMPONENT;
    if(restart_pending)return AB_NEEDS_RESTART;
    if(!first_ready||!second_ready)return AB_NEEDS_REVIEW;
    return AB_OPEN;
}
int ab_setup_action(int ready,int installed,int restart_pending) {
    if(restart_pending)return AB_RESTART;
    if(ready)return AB_KEEP;
    if(installed)return AB_REVIEW;
    return AB_INSTALL;
}
int ab_setup_installer_result(int result) {
    if(result==AB_CANCELLED)return AB_CANCELLED_RETRY;
    if(result==AB_FAILED)return AB_FAILED_RETRY;
    return AB_VERIFY; // A process exit is never proof of endpoint readiness.
}
int ab_setup_remove_shared_driver(void) {return 0;}
