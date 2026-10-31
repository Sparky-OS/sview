// MIT license; see docs/license-mit.txt.
#ifndef __StOutStereoKWin_h_
#define __StOutStereoKWin_h_
#include <StCore/StWindow.h>
#include <StThreads/StFPSControl.h>

/**
 * Per-eye canvas sizes the Stereo KWin build offers for the projected views
 * (theater, panoramas, cube maps). A flat picture keeps its own resolution in each eye.
 */
struct StStereoKWinResolution {
    enum { Own = 0, NB = 5 };
    static int width(int theId) {
        static const int THE_WIDTHS[NB] = { 0, 1280, 1920, 2560, 3840 };
        return theId > Own && theId < NB ? THE_WIDTHS[theId] : 0;
    }
    static int height(int theId) {
        static const int THE_HEIGHTS[NB] = { 0, 720, 1080, 1440, 2160 };
        return theId > Own && theId < NB ? THE_HEIGHTS[theId] : 0;
    }
    static StString name(int theId) {
        return theId > Own && theId < NB
             ? StString(width(theId)) + " x " + height(theId)
             : StString("The picture's own resolution");
    }
};

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
