#include "setup_policy.h"
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
