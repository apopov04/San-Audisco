#pragma once
// Shared, side-effect-free policy used by both native setup front ends.
// C ABI also lets Swift use the exact same decisions as Windows.
#ifdef __cplusplus
extern "C" {
#endif
enum ABSetupAction { AB_KEEP=0, AB_INSTALL=1, AB_REVIEW=2, AB_RESTART=3 };
enum ABInstallerResult { AB_RETURNED=0, AB_CANCELLED=1, AB_FAILED=2 };
enum ABSetupResult { AB_VERIFY=0, AB_CANCELLED_RETRY=1, AB_FAILED_RETRY=2 };
enum ABSetupStep { AB_CHECK=0, AB_APP=1, AB_FIRST_COMPONENT=2, AB_SECOND_COMPONENT=3, AB_NEEDS_REVIEW=4, AB_NEEDS_RESTART=5, AB_OPEN=6 };
int ab_setup_action(int ready, int installed, int restart_pending);
// Complete only missing components before offering a restart. Existing but
// unusable components require review, never a blind reinstall.
int ab_setup_next(int verified, int app_current, int first_ready, int first_present,
                  int second_ready, int second_present, int restart_pending);
int ab_setup_installer_result(int result);
int ab_setup_remove_shared_driver(void);
#ifdef __cplusplus
}
#endif
