#pragma once
// Shared, side-effect-free policy used by both native setup front ends.
// C ABI also lets Swift use the exact same decisions as Windows.
#ifdef __cplusplus
extern "C" {
#endif
enum ABSetupAction { AB_KEEP=0, AB_INSTALL=1, AB_REVIEW=2, AB_RESTART=3 };
enum ABInstallerResult { AB_RETURNED=0, AB_CANCELLED=1, AB_FAILED=2 };
enum ABSetupResult { AB_VERIFY=0, AB_CANCELLED_RETRY=1, AB_FAILED_RETRY=2 };
int ab_setup_action(int ready, int installed, int restart_pending);
int ab_setup_installer_result(int result);
int ab_setup_remove_shared_driver(void);
#ifdef __cplusplus
}
#endif
