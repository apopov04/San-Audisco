#import <AVFoundation/AVFoundation.h>
#include "bridge/agent_platform.hpp"
namespace bridge::agent {
bool capture_authorized(){return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]==AVAuthorizationStatusAuthorized;}
}
