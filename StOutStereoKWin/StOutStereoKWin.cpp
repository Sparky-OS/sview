// MIT license; see docs/license-mit.txt.
#include "StOutStereoKWin.h"

StOutStereoKWin::StOutStereoKWin(const std::shared_ptr<StResourceManager>& theResources, StNativeWin_t theParent)
: StWindow(theResources, theParent) {
    setCompositorStereo(true);
}
const char* StOutStereoKWin::getRendererId() const { return "StOutStereoKWin"; }
const char* StOutStereoKWin::getDeviceId() const { return "StereoKWin"; }
StString StOutStereoKWin::getRendererAbout() const { return "sView / Stereo KWin"; }
void StOutStereoKWin::getDevices(StOutDevicesList& theList) const {
    std::shared_ptr<StOutDevice> aDevice = std::make_shared<StOutDevice>();
    aDevice->PluginId = "StOutStereoKWin";
    aDevice->DeviceId = "StereoKWin";
    aDevice->Name = "Stereo KWin";
    aDevice->Priority = ST_DEVICE_SUPPORT_PREFER;
    theList.push_back(aDevice);
}
void StOutStereoKWin::stglDraw() {
    stglMakeCurrent(ST_WIN_MASTER);
    myFPSControl.setTargetFPS(getTargetFps());
    StWindow::stglDraw();
    myFPSControl.sleepToTarget();
    ++myFPSControl;
}
