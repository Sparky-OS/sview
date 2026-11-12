// MIT license; see docs/license-mit.txt.
#include <StImage/StPictureCorrection.h>
#include <StImage/StJpegParser.h>
#include <StAV/StAVImage.h>
#include <StGL/StParams.h>
#include <climits>
#include <cstring>

namespace {
    int layout(StFormat theFormat) {
        switch(theFormat) {
            case StFormat_Mono: return 0;
            case StFormat_Rows: case StFormat_Rows_RL: return 1;
            case StFormat_SideBySide_LR: case StFormat_SideBySide_RL: return 2;
            case StFormat_TopBottom_LR: case StFormat_TopBottom_RL: return 3;
            case StFormat_AnaglyphRedCyan: return 4;
            case StFormat_SeparateFrames: return 5;
            default: return -1;
        }
    }
    bool rightFirst(StFormat theFormat) {
        return theFormat == StFormat_SideBySide_RL || theFormat == StFormat_TopBottom_RL
            || theFormat == StFormat_Rows_RL || theFormat == StFormat_AnaglyphRedCyan;
    }
    bool equalImages(StAVImage& a, StAVImage& b) {
        if(a.isNull() || b.isNull() || a.getSizeX() != b.getSizeX() || a.getSizeY() != b.getSizeY()) { return false; }
        for(size_t p = 0; p < 4; ++p) {
            const StImagePlane& x = a.getPlane(p);
            const StImagePlane& y = b.getPlane(p);
            if(x.isNull() != y.isNull()) { return false; }
            if(x.isNull()) { continue; }
            if(x.getFormat() != y.getFormat() || x.getSizeX() != y.getSizeX() || x.getSizeY() != y.getSizeY()) { return false; }
            for(size_t row = 0; row < x.getSizeY(); ++row) {
                if(std::memcmp(x.getData(row), y.getData(row), x.getSizeX() * x.getSizePixelBytes())) { return false; }
            }
        }
        return true;
    }
    bool decode(StAVImage& theImage, const std::vector<stUByte_t>& theData, StImageFile::ImageType theType,
                 size_t theOffset, size_t theSize) {
        return theOffset <= theData.size() && theSize <= theData.size() - theOffset && theSize <= INT_MAX
            && theImage.loadExtra(StString(), theType, const_cast<uint8_t*>(theData.data() + theOffset), int(theSize), false);
    }
}
void StPictureCorrection::clearSaved(StStereoParams& theParams, bool theCarriesEyeOrder) {
    theParams.InputFormat = StFormat_AUTO;
    if(theCarriesEyeOrder || st::formatReversed(theParams.StereoFormat) != theParams.StereoFormat) {
        theParams.setSwapLR(false);
    }
}
int StPictureCorrection::prepare(const std::vector<stUByte_t>& theOriginal, StFormat theFormat,
                                  bool theSwapMpo, std::vector<stUByte_t>& theCandidate) {
    stereo_jpeg_info anInfo;
    const int aRead = stereo_jpeg_inspect(theOriginal.data(), theOriginal.size(), &anInfo);
    if(aRead != STEREO_JPEG_OK) { return aRead; }
    const int aLayout = layout(theFormat);
    const bool isRightFirst = anInfo.layout == 5 ? (bool(anInfo.right_first) != theSwapMpo) : rightFirst(theFormat);
    if(theOriginal.size() > theCandidate.max_size() - 65537) { return STEREO_JPEG_CAPACITY; }
    theCandidate.resize(theOriginal.size() + 65537);
    size_t aSize = 0;
    const int aResult = stereo_jpeg_correct(theOriginal.data(), theOriginal.size(), aLayout, isRightFirst,
                                           theCandidate.data(), theCandidate.size(), &aSize);
    if(aResult != STEREO_JPEG_OK) { theCandidate.clear(); return aResult; }
    theCandidate.resize(aSize);
    return aResult;
}
bool StPictureCorrection::samePixels(const std::vector<stUByte_t>& theOriginal,
                                      const std::vector<stUByte_t>& theCandidate, StImageFile::ImageType theType) {
    StAVImage a, b;
    return decode(a, theOriginal, theType, 0, theOriginal.size())
        && decode(b, theCandidate, theType, 0, theCandidate.size()) && equalImages(a, b);
}
bool StPictureCorrection::verify(const std::vector<stUByte_t>& theOriginal,
                                  const std::vector<stUByte_t>& theCandidate, StFormat theFormat, bool theSwapMpo) {
    stereo_jpeg_info a, b;
    if(stereo_jpeg_inspect(theOriginal.data(), theOriginal.size(), &a)
    || stereo_jpeg_inspect(theCandidate.data(), theCandidate.size(), &b) || b.lossless || b.layout != layout(theFormat)) { return false; }
    if(b.layout != 5) {
        if(b.layout && bool(b.right_first) != rightFirst(theFormat)) { return false; }
        StJpegParser aReader;
        aReader.wrapBuffer(const_cast<stUByte_t*>(theCandidate.data()), theCandidate.size());
        aReader.setDataSize(theCandidate.size());
        return aReader.parse() && aReader.getSrcFormat() == theFormat
            && samePixels(theOriginal, theCandidate, StImageFile::ST_TYPE_JPEG);
    }
    if(b.right_first != (a.right_first != theSwapMpo)) { return false; }
    for(unsigned eye = 0; eye < 2; ++eye) {
        unsigned before = eye ^ unsigned(a.right_first) ^ unsigned(theSwapMpo), after = eye ^ unsigned(b.right_first);
        StAVImage x, y;
        if(!decode(x, theOriginal, StImageFile::ST_TYPE_JPEG, a.image_offset[before], a.image_size[before])
        || !decode(y, theCandidate, StImageFile::ST_TYPE_JPEG, b.image_offset[after], b.image_size[after]) || !equalImages(x, y)) { return false; }
    }
    return true;
}
