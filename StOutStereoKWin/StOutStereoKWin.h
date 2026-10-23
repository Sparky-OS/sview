// MIT license; see docs/license-mit.txt.
#ifndef __StOutStereoKWin_h_
#define __StOutStereoKWin_h_
#include <StCore/StWindow.h>
#include <StThreads/StFPSControl.h>

class StOutStereoKWin : public StWindow {
public:
    ST_CPPEXPORT StOutStereoKWin(const std::shared_ptr<StResourceManager>& theResources, StNativeWin_t theParent);
    ST_CPPEXPORT virtual const char* getRendererId() const ST_ATTR_OVERRIDE;
    ST_CPPEXPORT virtual const char* getDeviceId() const ST_ATTR_OVERRIDE;
    ST_CPPEXPORT virtual StString getRendererAbout() const ST_ATTR_OVERRIDE;
    ST_CPPEXPORT virtual void getDevices(StOutDevicesList& theList) const ST_ATTR_OVERRIDE;
    ST_CPPEXPORT virtual void stglDraw() ST_ATTR_OVERRIDE;
private:
    StFPSControl myFPSControl;
};
#endif
