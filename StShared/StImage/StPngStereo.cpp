// PNG sTER chunk: the stereoscopic layout written into a PNG datastream, every other byte kept.
// MIT license; see docs/license-mit.txt.

#include <StImage/StPngStereo.h>

#include <StStrings/StString.h>

#include <cstring>

namespace {

    static const stUByte_t THE_PNG_SIGNATURE[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    inline uint32_t readU32BE(const stUByte_t* theData) {
        return (uint32_t(theData[0]) << 24) | (uint32_t(theData[1]) << 16) | (uint32_t(theData[2]) << 8) | uint32_t(theData[3]);
    }

    inline void appendU32BE(std::vector<stUByte_t>& theBuffer, const uint32_t theValue) {
        theBuffer.push_back(stUByte_t(theValue >> 24));
        theBuffer.push_back(stUByte_t(theValue >> 16));
        theBuffer.push_back(stUByte_t(theValue >> 8));
        theBuffer.push_back(stUByte_t(theValue));
    }

    inline bool isChunk(const stUByte_t* theType, const char* theName) {
        return theType[0] == stUByte_t(theName[0]) && theType[1] == stUByte_t(theName[1])
            && theType[2] == stUByte_t(theName[2]) && theType[3] == stUByte_t(theName[3]);
    }

    /**
     * One chunk of the datastream: its offset (the length field) and the size of its data.
     */
    struct StPngChunk {
        size_t   Offset;
        uint32_t Length;
    };

    /**
     * Split the datastream into chunks (PNG specification, 5.3: length, type, data, CRC);
     * IHDR must be the first chunk (5.6), IDAT and IEND must be present.
     */
    static bool readChunks(const stUByte_t* theData, const size_t theSize, std::vector<StPngChunk>& theChunks) {
        if(theData == NULL || theSize < 8 || std::memcmp(theData, THE_PNG_SIGNATURE, 8) != 0) {
            return false;
        }
        bool hasIdat = false, hasIend = false;
        for(size_t anOffset = 8; anOffset < theSize;) {
            if(theSize - anOffset < 12) {
                return false;
            }
            const uint32_t aLength = readU32BE(theData + anOffset);
            if(aLength > 0x7FFFFFFFu || theSize - anOffset - 12 < aLength) {
                return false;
            }
            const stUByte_t* aType = theData + anOffset + 4;
            if(theChunks.empty() && (!isChunk(aType, "IHDR") || aLength != 13)) {
                return false;
            }
            hasIdat = hasIdat || isChunk(aType, "IDAT");
            hasIend = hasIend || isChunk(aType, "IEND");
            StPngChunk aChunk = { anOffset, aLength };
            theChunks.push_back(aChunk);
            anOffset += 12 + size_t(aLength);
            if(hasIend) {
                break;
            }
        }
        return hasIdat && hasIend;
    }

}

uint32_t StPngStereo::chunkCrc(const stUByte_t* theTypeAndData,
                               const size_t     theSize) {
    static uint32_t THE_TABLE[256] = {};
    static bool isTableReady = false;
    if(!isTableReady) {
        for(uint32_t anIter = 0; anIter < 256; ++anIter) {
            uint32_t aValue = anIter;
            for(int aBit = 0; aBit < 8; ++aBit) {
                aValue = (aValue & 1) != 0 ? (0xEDB88320u ^ (aValue >> 1)) : (aValue >> 1);
            }
            THE_TABLE[anIter] = aValue;
        }
        isTableReady = true;
    }
    uint32_t aCrc = 0xFFFFFFFFu;
    for(size_t anIter = 0; anIter < theSize; ++anIter) {
        aCrc = THE_TABLE[(aCrc ^ theTypeAndData[anIter]) & 0xFF] ^ (aCrc >> 8);
    }
    return aCrc ^ 0xFFFFFFFFu;
}

StFormat StPngStereo::readSter(const stUByte_t* theData,
                               const size_t     theSize) {
    std::vector<StPngChunk> aChunks;
    if(!readChunks(theData, theSize, aChunks)) {
        return StFormat_AUTO;
    }
    for(size_t aChunkIter = 0; aChunkIter < aChunks.size(); ++aChunkIter) {
        const StPngChunk& aChunk = aChunks[aChunkIter];
        if(isChunk(theData + aChunk.Offset + 4, "sTER") && aChunk.Length == 1) {
            const stUByte_t aMode = theData[aChunk.Offset + 8];
            return aMode == 1 ? StFormat_SideBySide_LR : (aMode == 0 ? StFormat_SideBySide_RL : StFormat_AUTO);
        }
    }
    return StFormat_AUTO;
}

StPngStereo::Result StPngStereo::setupSter(const stUByte_t*        theData,
                                           const size_t            theSize,
                                           const StFormat          theFormat,
                                           std::vector<stUByte_t>& theResult,
                                           uint32_t&               theWidth) {
    theResult.clear();
    theWidth = 0;
    std::vector<StPngChunk> aChunks;
    if(!readChunks(theData, theSize, aChunks)) {
        return Result_NotPng;
    }
    theWidth = readU32BE(theData + aChunks[0].Offset + 8);

    // mode 0: cross-fuse layout, the right-eye image at the left; mode 1: diverging-fuse layout, the left-eye image at the left
    int aMode = -1;
    switch(theFormat) {
        case StFormat_SideBySide_RL: aMode = 0; break;
        case StFormat_SideBySide_LR: aMode = 1; break;
        case StFormat_Mono:          break;
        default:                     return Result_Layout;
    }

    int aStereoChunks = 0, aCurrentMode = -1;
    for(size_t aChunkIter = 0; aChunkIter < aChunks.size(); ++aChunkIter) {
        const StPngChunk& aChunk = aChunks[aChunkIter];
        if(isChunk(theData + aChunk.Offset + 4, "sTER")) {
            ++aStereoChunks;
            aCurrentMode = aChunk.Length == 1 ? int(theData[aChunk.Offset + 8]) : -1;
        }
    }
    if(aMode == -1 && aStereoChunks == 0) {
        return Result_MonoNoMark;
    }
    if(aMode != -1) {
        // the decoder reads padding = 15 - ((W - 1) mod 16) and the subimage width (W - padding) / 2;
        // the picture splits in its middle, as it is, only when that padding is 0
        if(theWidth < 16 || 15 - ((theWidth - 1) % 16) != 0) {
            return Result_Width;
        }
        if(aStereoChunks == 1 && aCurrentMode == aMode) {
            return Result_Unchanged;
        }
    }

    // the signature and IHDR, then the one sTER chunk (it must appear before the first IDAT chunk),
    // then every other chunk as it was
    theResult.reserve(theSize + 13);
    const size_t anIhdrEnd = aChunks[0].Offset + 12 + aChunks[0].Length;
    theResult.insert(theResult.end(), theData, theData + anIhdrEnd);
    if(aMode != -1) {
        const stUByte_t aChunkData[5] = { 's', 'T', 'E', 'R', stUByte_t(aMode) };
        appendU32BE(theResult, 1);
        theResult.insert(theResult.end(), aChunkData, aChunkData + 5);
        appendU32BE(theResult, chunkCrc(aChunkData, 5));
    }
    for(size_t aChunkIter = 1; aChunkIter < aChunks.size(); ++aChunkIter) {
        const StPngChunk& aChunk = aChunks[aChunkIter];
        if(isChunk(theData + aChunk.Offset + 4, "sTER")) {
            continue;
        }
        theResult.insert(theResult.end(), theData + aChunk.Offset, theData + aChunk.Offset + 12 + aChunk.Length);
    }
    // anything after IEND is kept too
    const StPngChunk& aLast = aChunks.back();
    const size_t anEnd = aLast.Offset + 12 + aLast.Length;
    theResult.insert(theResult.end(), theData + anEnd, theData + theSize);
    return Result_Written;
}
