// PNG sTER chunk (Extensions to the PNG 1.2 Specification, version 1.4.0, section 3.6): what is written is
// one chunk before the image data, with the mode its eye order means, and nothing else changes.
// MIT license; see docs/license-mit.txt.
#include <StImage/StPngStereo.h>

extern "C" {
#include <libavutil/crc.h>
}

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static int THE_FAILURES = 0;

static void check(bool theCondition, const char* theCase, const char* theWhat) {
    if(!theCondition) {
        std::printf("FAIL %s: %s\n", theCase, theWhat);
        ++THE_FAILURES;
    }
}

static void appendU32(std::vector<unsigned char>& theData, uint32_t theValue) {
    theData.push_back((unsigned char )(theValue >> 24));
    theData.push_back((unsigned char )(theValue >> 16));
    theData.push_back((unsigned char )(theValue >> 8));
    theData.push_back((unsigned char )theValue);
}

static uint32_t readU32(const unsigned char* theData) {
    return (uint32_t(theData[0]) << 24) | (uint32_t(theData[1]) << 16) | (uint32_t(theData[2]) << 8) | uint32_t(theData[3]);
}

// the CRC by FFmpeg's own table (ISO 3309, as the PNG specification's Annex D), not by the code under test
static uint32_t referenceCrc(const unsigned char* theData, size_t theSize) {
    return av_crc(av_crc_get_table(AV_CRC_32_IEEE_LE), 0xFFFFFFFFu, theData, theSize) ^ 0xFFFFFFFFu;
}

static void appendChunk(std::vector<unsigned char>& theData, const char* theType, const std::vector<unsigned char>& theBody) {
    appendU32(theData, uint32_t(theBody.size()));
    std::vector<unsigned char> aCrcData(theType, theType + 4);
    aCrcData.insert(aCrcData.end(), theBody.begin(), theBody.end());
    theData.insert(theData.end(), aCrcData.begin(), aCrcData.end());
    appendU32(theData, referenceCrc(aCrcData.data(), aCrcData.size()));
}

// a datastream: signature, IHDR (8-bit RGB), the optional sTER, IDAT, a tEXt after the image data, IEND
static std::vector<unsigned char> makePng(uint32_t theWidth, int theSterMode, bool theSterAfterIdat = false) {
    static const unsigned char THE_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    std::vector<unsigned char> aData(THE_SIGNATURE, THE_SIGNATURE + 8);
    std::vector<unsigned char> anIhdr;
    appendU32(anIhdr, theWidth);
    appendU32(anIhdr, 4);
    anIhdr.push_back(8); anIhdr.push_back(2); anIhdr.push_back(0); anIhdr.push_back(0); anIhdr.push_back(0);
    appendChunk(aData, "IHDR", anIhdr);
    if(theSterMode >= 0 && !theSterAfterIdat) {
        appendChunk(aData, "sTER", std::vector<unsigned char>(1, (unsigned char )theSterMode));
    }
    appendChunk(aData, "IDAT", std::vector<unsigned char>(37, 0x5A));
    if(theSterMode >= 0 && theSterAfterIdat) {
        appendChunk(aData, "sTER", std::vector<unsigned char>(1, (unsigned char )theSterMode));
    }
    const char aText[] = "Comment\0kept";
    appendChunk(aData, "tEXt", std::vector<unsigned char>(aText, aText + sizeof(aText) - 1));
    appendChunk(aData, "IEND", std::vector<unsigned char>());
    return aData;
}

struct Chunk { std::string Type; std::vector<unsigned char> Body; bool IsCrcValid; };

// an independent reader of the result: every chunk, in order, with its CRC checked
static std::vector<Chunk> readChunks(const std::vector<unsigned char>& theData, bool& theIsValid) {
    std::vector<Chunk> aChunks;
    theIsValid = theData.size() >= 8;
    for(size_t aPos = 8; theIsValid && aPos < theData.size();) {
        if(theData.size() - aPos < 12) { theIsValid = false; break; }
        const uint32_t aLength = readU32(&theData[aPos]);
        if(theData.size() - aPos - 12 < aLength) { theIsValid = false; break; }
        Chunk aChunk;
        aChunk.Type = std::string((const char* )&theData[aPos + 4], 4);
        aChunk.Body.assign(theData.begin() + aPos + 8, theData.begin() + aPos + 8 + aLength);
        aChunk.IsCrcValid = referenceCrc(&theData[aPos + 4], 4 + aLength) == readU32(&theData[aPos + 8 + aLength]);
        aChunks.push_back(aChunk);
        aPos += 12 + aLength;
    }
    return aChunks;
}

static std::string order(const std::vector<Chunk>& theChunks) {
    std::string anOrder;
    for(size_t anIter = 0; anIter < theChunks.size(); ++anIter) {
        anOrder += (anIter != 0 ? " " : "") + theChunks[anIter].Type;
    }
    return anOrder;
}

static void checkWritten(const char* theCase, const std::vector<unsigned char>& theSource, StFormat theFormat,
                         const char* theOrder, int theMode) {
    std::vector<stUByte_t> aResult;
    uint32_t aWidth = 0;
    const StPngStereo::Result aRes = StPngStereo::setupSter(theSource.data(), theSource.size(), theFormat, aResult, aWidth);
    check(aRes == StPngStereo::Result_Written, theCase, "written");
    bool isValid = false;
    const std::vector<Chunk> aChunks = readChunks(std::vector<unsigned char>(aResult.begin(), aResult.end()), isValid);
    check(isValid, theCase, "a datastream of whole chunks");
    check(order(aChunks) == theOrder, theCase, (std::string("chunk order ") + order(aChunks)).c_str());
    for(size_t anIter = 0; anIter < aChunks.size(); ++anIter) {
        check(aChunks[anIter].IsCrcValid, theCase, ("CRC of " + aChunks[anIter].Type).c_str());
        if(aChunks[anIter].Type == "sTER") {
            check(aChunks[anIter].Body.size() == 1 && aChunks[anIter].Body[0] == theMode, theCase, "the mode byte");
        }
    }
    // every other chunk byte for byte
    bool isSourceValid = false;
    const std::vector<Chunk> aSourceChunks = readChunks(theSource, isSourceValid);
    for(size_t anIter = 0; anIter < aSourceChunks.size(); ++anIter) {
        if(aSourceChunks[anIter].Type == "sTER") {
            continue;
        }
        bool isKept = false;
        for(size_t aResIter = 0; aResIter < aChunks.size(); ++aResIter) {
            isKept = isKept || (aChunks[aResIter].Type == aSourceChunks[anIter].Type && aChunks[aResIter].Body == aSourceChunks[anIter].Body);
        }
        check(isKept, theCase, ("kept " + aSourceChunks[anIter].Type).c_str());
    }
    check(StPngStereo::readSter(aResult.data(), aResult.size())
       == (theMode == 1 ? StFormat_SideBySide_LR : theMode == 0 ? StFormat_SideBySide_RL : StFormat_AUTO), theCase, "read back");
}

static void checkRefused(const char* theCase, const std::vector<unsigned char>& theSource, StFormat theFormat,
                         StPngStereo::Result theExpected) {
    std::vector<stUByte_t> aResult;
    uint32_t aWidth = 0;
    const StPngStereo::Result aRes = StPngStereo::setupSter(theSource.data(), theSource.size(), theFormat, aResult, aWidth);
    check(aRes == theExpected, theCase, "refused with its reason");
    check(aResult.empty(), theCase, "nothing to write");
}

int main() {
    // a 32-pixel picture: views of 16, the decoder's padding 15 - ((32 - 1) mod 16) is 0
    checkWritten("sbs-lr", makePng(32, -1), StFormat_SideBySide_LR, "IHDR sTER IDAT tEXt IEND", 1);
    checkWritten("sbs-rl", makePng(32, -1), StFormat_SideBySide_RL, "IHDR sTER IDAT tEXt IEND", 0);
    checkWritten("swap-existing", makePng(32, 1), StFormat_SideBySide_RL, "IHDR sTER IDAT tEXt IEND", 0);
    checkWritten("after-idat-moved", makePng(64, 0, true), StFormat_SideBySide_LR, "IHDR sTER IDAT tEXt IEND", 1);
    checkWritten("mono-removes", makePng(32, 1), StFormat_Mono, "IHDR IDAT tEXt IEND", -1);
    checkWritten("wide", makePng(3840, -1), StFormat_SideBySide_LR, "IHDR sTER IDAT tEXt IEND", 1);

    std::vector<stUByte_t> aResult;
    uint32_t aWidth = 0;
    const std::vector<unsigned char> aMarked = makePng(32, 1);
    check(StPngStereo::setupSter(aMarked.data(), aMarked.size(), StFormat_SideBySide_LR, aResult, aWidth) == StPngStereo::Result_Unchanged,
          "unchanged", "the same mode is already there");

    checkWritten("width-48", makePng(48, -1), StFormat_SideBySide_RL, "IHDR sTER IDAT tEXt IEND", 0);
    // the right view would start at column 15, 20, 9 or 4: not divisible by 8
    checkRefused("width-30", makePng(30, -1), StFormat_SideBySide_LR, StPngStereo::Result_Width);
    checkRefused("width-40", makePng(40, -1), StFormat_SideBySide_RL, StPngStereo::Result_Width);
    checkRefused("width-18", makePng(18, -1), StFormat_SideBySide_LR, StPngStereo::Result_Width);
    checkRefused("width-8", makePng(8, -1), StFormat_SideBySide_LR, StPngStereo::Result_Width);
    const StFormat THE_OTHERS[] = { StFormat_TopBottom_LR, StFormat_TopBottom_RL, StFormat_Rows, StFormat_Rows_RL,
                                    StFormat_Columns, StFormat_Columns_RL, StFormat_Checkerboard, StFormat_Checkerboard_RL,
                                    StFormat_Quincunx, StFormat_Quincunx_RL, StFormat_AnaglyphRedCyan,
                                    StFormat_AnaglyphGreenMagenta, StFormat_AnaglyphYellowBlue, StFormat_Tiled4x,
                                    StFormat_FrameSequence, StFormat_SeparateFrames };
    for(size_t anIter = 0; anIter < sizeof(THE_OTHERS) / sizeof(THE_OTHERS[0]); ++anIter) {
        checkRefused(("layout-" + std::to_string(int(THE_OTHERS[anIter]))).c_str(), makePng(32, -1), THE_OTHERS[anIter],
                     StPngStereo::Result_Layout);
    }
    checkRefused("mono-unmarked", makePng(32, -1), StFormat_Mono, StPngStereo::Result_MonoNoMark);

    std::vector<unsigned char> aTruncated = makePng(32, -1);
    aTruncated.resize(aTruncated.size() - 5);
    checkRefused("truncated", aTruncated, StFormat_SideBySide_LR, StPngStereo::Result_NotPng);
    std::vector<unsigned char> aNotPng = makePng(32, -1);
    aNotPng[1] = 'Q';
    checkRefused("signature", aNotPng, StFormat_SideBySide_LR, StPngStereo::Result_NotPng);

    // bytes after IEND are kept
    std::vector<unsigned char> aTrailing = makePng(32, -1);
    aTrailing.push_back(0x42);
    check(StPngStereo::setupSter(aTrailing.data(), aTrailing.size(), StFormat_SideBySide_LR, aResult, aWidth) == StPngStereo::Result_Written
       && aResult.size() == aTrailing.size() + 13 && aResult.back() == 0x42, "trailing", "bytes after IEND kept");

    std::printf("%d failing checks\n", THE_FAILURES);
    return THE_FAILURES == 0 ? 0 : 1;
}
