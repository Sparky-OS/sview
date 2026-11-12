// Metadata correction adapter; descriptor bytes belong to libstereo-policy.
// MIT license; see docs/license-mit.txt.
#ifndef __StPictureCorrection_h_
#define __StPictureCorrection_h_
#include <StGLStereo/StFormatEnum.h>
#include <StImage/StImageFile.h>
#include <vector>
#include <stereo-jpeg.h>
class StStereoParams;

class StPictureCorrection {
public:
    ST_CPPEXPORT static void clearSaved(StStereoParams& theParams, bool theCarriesEyeOrder = false);
    ST_CPPEXPORT static int prepare(const std::vector<stUByte_t>& theOriginal, StFormat theFormat,
                                    bool theSwapMpo, std::vector<stUByte_t>& theCandidate);
    ST_CPPEXPORT static bool verify(const std::vector<stUByte_t>& theOriginal,
                                    const std::vector<stUByte_t>& theCandidate, StFormat theFormat, bool theSwapMpo);
    ST_CPPEXPORT static bool samePixels(const std::vector<stUByte_t>& theOriginal,
                                        const std::vector<stUByte_t>& theCandidate, StImageFile::ImageType theType);
};
#endif
