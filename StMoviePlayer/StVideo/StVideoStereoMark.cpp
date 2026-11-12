// The 3D layout of a video written into its container's own mark: Matroska and WebM StereoMode
// (RFC 9559 5.1.4.1.28.3, written with MKVToolNix's mkvpropedit), MP4 and MOV st3d (Spherical Video V2 RFC).
// MIT license; see docs/license-mit.txt.

#include "StVideoStereoMark.h"
#include "StVideoQueue.h"

#include <StAV/stAV.h>
#include <StFile/StFileNode.h>
#include <StFile/StRawFile.h>
#include <StFile/StFileTransaction.h>
#include <StThreads/StProcess.h>
extern "C" {
#include <libavutil/hash.h>
}

#if defined(__linux__) && !defined(__ANDROID__)
    #include <cerrno>
    #include <cstring>
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/stat.h>
#endif

namespace {

    inline uint32_t readU32BE(const stUByte_t* theData) {
        return (uint32_t(theData[0]) << 24) | (uint32_t(theData[1]) << 16) | (uint32_t(theData[2]) << 8) | uint32_t(theData[3]);
    }

    inline uint64_t readU64BE(const stUByte_t* theData) {
        return (uint64_t(readU32BE(theData)) << 32) | uint64_t(readU32BE(theData + 4));
    }

    inline void writeU32BE(stUByte_t* theData, const uint32_t theValue) {
        theData[0] = stUByte_t(theValue >> 24);
        theData[1] = stUByte_t(theValue >> 16);
        theData[2] = stUByte_t(theValue >> 8);
        theData[3] = stUByte_t(theValue);
    }

    inline void writeU64BE(stUByte_t* theData, const uint64_t theValue) {
        writeU32BE(theData,     uint32_t(theValue >> 32));
        writeU32BE(theData + 4, uint32_t(theValue));
    }

    inline uint32_t fourCC(const char* theName) {
        return (uint32_t(stUByte_t(theName[0])) << 24) | (uint32_t(stUByte_t(theName[1])) << 16)
             | (uint32_t(stUByte_t(theName[2])) << 8)  |  uint32_t(stUByte_t(theName[3]));
    }

    /**
     * One box of an ISO base media file (ISO/IEC 14496-12, Box: size, type, largesize when size is 1,
     * size 0 for the last box of the file).
     */
    struct StMp4Box {
        size_t   Offset;
        size_t   HeaderSize;
        uint64_t Size;
        uint32_t Type;
        bool     IsToEnd;
    };

    static bool readBox(const std::vector<stUByte_t>& theData, const size_t thePos, const size_t theEnd, StMp4Box& theBox) {
        if(thePos > theEnd || theEnd - thePos < 8) {
            return false;
        }
        theBox.Offset     = thePos;
        theBox.HeaderSize = 8;
        theBox.Size       = readU32BE(&theData[thePos]);
        theBox.Type       = readU32BE(&theData[thePos + 4]);
        theBox.IsToEnd    = false;
        if(theBox.Size == 1) {
            if(theEnd - thePos < 16) {
                return false;
            }
            theBox.Size       = readU64BE(&theData[thePos + 8]);
            theBox.HeaderSize = 16;
        } else if(theBox.Size == 0) {
            theBox.Size    = theEnd - thePos;
            theBox.IsToEnd = true;
        }
        return theBox.Size >= theBox.HeaderSize && theBox.Size <= uint64_t(theEnd - thePos);
    }

    /**
     * Find the first child box of the given type, starting at theStart inside the parent.
     */
    static bool findChild(const std::vector<stUByte_t>& theData, const StMp4Box& theParent, const size_t theStart,
                          const uint32_t theType, StMp4Box& theChild) {
        const size_t anEnd = theParent.Offset + size_t(theParent.Size);
        for(size_t aPos = theStart; aPos < anEnd;) {
            StMp4Box aBox;
            if(!readBox(theData, aPos, anEnd, aBox)) {
                return false;
            }
            if(aBox.Type == theType) {
                theChild = aBox;
                return true;
            }
            aPos += size_t(aBox.Size);
        }
        return false;
    }

    static bool growBox(std::vector<stUByte_t>& theData, const StMp4Box& theBox, const uint64_t theGrowth) {
        if(theBox.IsToEnd) {
            return true;
        } else if(theBox.HeaderSize == 16) {
            writeU64BE(&theData[theBox.Offset + 8], theBox.Size + theGrowth);
            return true;
        } else if(theBox.Size + theGrowth > 0xFFFFFFFFu) {
            return false;
        }
        writeU32BE(&theData[theBox.Offset], uint32_t(theBox.Size + theGrowth));
        return true;
    }

    /**
     * Read the EBML header's DocType (RFC 8794, 11.2.6: "matroska" or "webm").
     */
    static StString readDocType(const StString& thePath) {
        StRawFile aFile;
        if(!aFile.readFile(thePath, -1, 4096)) {
            return StString();
        }
        const stUByte_t* aData = aFile.getBuffer();
        const size_t     aSize = aFile.getSize();
        // a variable-size integer: the leading zero bits give its length
        auto readVint = [aData, aSize](size_t& thePos, uint64_t& theValue, const bool theToKeepMarker) -> bool {
            if(thePos >= aSize || aData[thePos] == 0) {
                return false;
            }
            int aLength = 1;
            while((aData[thePos] & (0x80 >> (aLength - 1))) == 0) {
                ++aLength;
            }
            if(thePos + aLength > aSize) {
                return false;
            }
            theValue = theToKeepMarker ? aData[thePos] : (aData[thePos] & (0xFF >> aLength));
            for(int anIter = 1; anIter < aLength; ++anIter) {
                theValue = (theValue << 8) | aData[thePos + anIter];
            }
            thePos += aLength;
            return true;
        };
        size_t aPos = 0;
        uint64_t anId = 0, aHeaderSize = 0;
        if(!readVint(aPos, anId, true) || anId != 0x1A45DFA3
        || !readVint(aPos, aHeaderSize, false)) {
            return StString();
        }
        const size_t anEnd = stMin(aSize, aPos + size_t(aHeaderSize));
        while(aPos < anEnd) {
            uint64_t aChildId = 0, aChildSize = 0;
            if(!readVint(aPos, aChildId, true) || !readVint(aPos, aChildSize, false) || aPos + aChildSize > anEnd) {
                return StString();
            }
            if(aChildId == 0x4282) {
                return StString((const char* )aData + aPos, size_t(aChildSize));
            }
            aPos += size_t(aChildSize);
        }
        return StString();
    }

    inline bool isSameLayout(const StFormat theFormat1, const bool theIsRightFirst1,
                             const StFormat theFormat2, const bool theIsRightFirst2) {
        return theFormat1 == theFormat2
            && (theFormat1 != StFormat_FrameSequence || theIsRightFirst1 == theIsRightFirst2);
    }

    // Metadata edits must preserve all compressed packets and codec configuration.
    static bool mediaDigest(const StString& thePath, std::vector<stUByte_t>& theDigest) {
        AVFormatContext* aCtx = NULL;
        AVHashContext* aHash = NULL;
        if(avformat_open_input(&aCtx, thePath.toCString(), NULL, NULL) < 0) { return false; }
        bool isOk = avformat_find_stream_info(aCtx, NULL) >= 0 && av_hash_alloc(&aHash, "sha256") >= 0;
        if(isOk) {
            av_hash_init(aHash);
            for(unsigned i = 0; i < aCtx->nb_streams; ++i) {
                const AVCodecParameters* aCodec = aCtx->streams[i]->codecpar;
                stUByte_t header[12];
                writeU32BE(header, i);
                writeU32BE(header + 4, uint32_t(aCodec->codec_id));
                writeU32BE(header + 8, uint32_t(aCodec->extradata_size));
                av_hash_update(aHash, header, sizeof(header));
                if(aCodec->extradata_size > 0) { av_hash_update(aHash, aCodec->extradata, size_t(aCodec->extradata_size)); }
            }
            AVPacket* aPacket = av_packet_alloc();
            if(aPacket == NULL) { isOk = false; }
            int aResult = 0;
            while(isOk && (aResult = av_read_frame(aCtx, aPacket)) >= 0) {
                stUByte_t header[8];
                writeU32BE(header, uint32_t(aPacket->stream_index));
                writeU32BE(header + 4, uint32_t(aPacket->size));
                av_hash_update(aHash, header, sizeof(header));
                av_hash_update(aHash, aPacket->data, size_t(aPacket->size));
                av_packet_unref(aPacket);
            }
            isOk = isOk && aResult == AVERROR_EOF;
            if(isOk) { theDigest.resize(32); av_hash_final_bin(aHash, theDigest.data(), theDigest.size()); }
            av_packet_free(&aPacket);
        }
        av_hash_freep(&aHash);
        avformat_close_input(&aCtx);
        return isOk;
    }


}

StVideoStereoMark::Container StVideoStereoMark::containerOf(const StString& theFormatName,
                                                            const StString& thePath) {
    if(theFormatName.isStartsWith(stCString("matroska"))) {
        return readDocType(thePath).isEquals(stCString("webm")) ? Container_WebM : Container_Matroska;
    } else if(theFormatName.isStartsWith(stCString("mov,mp4"))) {
        return Container_Mp4;
    }
    return Container_Other;
}

int StVideoStereoMark::matroskaStereoMode(const StFormat theFormat,
                                          const bool     theIsRightFirst,
                                          const bool     theIsWebM) {
    int aValue = -1;
    switch(theFormat) {
        case StFormat_Mono:                 aValue = 0;  break;
        case StFormat_SideBySide_LR:        aValue = 1;  break;
        case StFormat_TopBottom_RL:         aValue = 2;  break;
        case StFormat_TopBottom_LR:         aValue = 3;  break;
        case StFormat_Checkerboard_RL:      aValue = 4;  break;
        case StFormat_Checkerboard:         aValue = 5;  break;
        case StFormat_Rows_RL:              aValue = 6;  break;
        case StFormat_Rows:                 aValue = 7;  break;
        case StFormat_Columns_RL:           aValue = 8;  break;
        case StFormat_Columns:              aValue = 9;  break;
        case StFormat_AnaglyphRedCyan:      aValue = 10; break;
        case StFormat_SideBySide_RL:        aValue = 11; break;
        case StFormat_AnaglyphGreenMagenta: aValue = 12; break;
        // both eyes in turn: FFmpeg reads and writes 13 and 14 as its frame sequence
        case StFormat_FrameSequence:        aValue = theIsRightFirst ? 14 : 13; break;
        default: break;
    }
    if(theIsWebM && aValue != 0 && aValue != 1 && aValue != 2 && aValue != 3 && aValue != 11) {
        return -1;
    }
    return aValue;
}

int StVideoStereoMark::mp4StereoMode(const StFormat theFormat) {
    switch(theFormat) {
        case StFormat_Mono:          return 0;
        case StFormat_TopBottom_LR:  return 1;
        case StFormat_SideBySide_LR: return 2;
        // 4, Stereoscopic Right-Left, is defined, but FFmpeg's st3d reader takes only 0, 1 and 2
        default:                     return -1;
    }
}

StVideoStereoMark::Result StVideoStereoMark::setupMp4Moov(std::vector<stUByte_t>& theMoov,
                                                          const uint32_t          theTrackId,
                                                          const int               theMode,
                                                          size_t&                 theGrowth) {
    theGrowth = 0;
    StMp4Box aMoov;
    if(!readBox(theMoov, 0, theMoov.size(), aMoov) || aMoov.Type != fourCC("moov")) {
        return Result_Broken;
    }

    // moov > trak (tkhd track_ID) > mdia > minf > stbl > stsd > the visual sample entry
    std::vector<StMp4Box> aPath;
    StMp4Box anEntry;
    bool isFound = false;
    const size_t aMoovEnd = aMoov.Offset + size_t(aMoov.Size);
    for(size_t aPos = aMoov.Offset + aMoov.HeaderSize; aPos < aMoovEnd && !isFound;) {
        StMp4Box aTrak;
        if(!readBox(theMoov, aPos, aMoovEnd, aTrak)) {
            return Result_Broken;
        }
        aPos += size_t(aTrak.Size);
        StMp4Box aTkhd;
        if(aTrak.Type != fourCC("trak")
        || !findChild(theMoov, aTrak, aTrak.Offset + aTrak.HeaderSize, fourCC("tkhd"), aTkhd)) {
            continue;
        }
        // FullBox version 1 has 64-bit creation and modification times before track_ID
        const size_t aVersionPos = aTkhd.Offset + aTkhd.HeaderSize;
        const size_t anIdPos = aVersionPos + 4 + (theMoov[aVersionPos] == 1 ? 16 : 8);
        if(anIdPos + 4 > aTkhd.Offset + aTkhd.Size || readU32BE(&theMoov[anIdPos]) != theTrackId) {
            continue;
        }

        StMp4Box aMdia, aMinf, aStbl, aStsd;
        if(!findChild(theMoov, aTrak, aTrak.Offset + aTrak.HeaderSize, fourCC("mdia"), aMdia)
        || !findChild(theMoov, aMdia, aMdia.Offset + aMdia.HeaderSize, fourCC("minf"), aMinf)
        || !findChild(theMoov, aMinf, aMinf.Offset + aMinf.HeaderSize, fourCC("stbl"), aStbl)
        || !findChild(theMoov, aStbl, aStbl.Offset + aStbl.HeaderSize, fourCC("stsd"), aStsd)
        || aStsd.Size < aStsd.HeaderSize + 8) {
            return Result_Broken;
        }
        // stsd: FullBox, then entry_count, then the sample entries
        if(readU32BE(&theMoov[aStsd.Offset + aStsd.HeaderSize + 4]) != 1) {
            return Result_Mp4Entries;
        }
        if(!readBox(theMoov, aStsd.Offset + aStsd.HeaderSize + 8, aStsd.Offset + size_t(aStsd.Size), anEntry)) {
            return Result_Broken;
        }
        aPath.push_back(aMoov);
        aPath.push_back(aTrak);
        aPath.push_back(aMdia);
        aPath.push_back(aMinf);
        aPath.push_back(aStbl);
        aPath.push_back(aStsd);
        aPath.push_back(anEntry);
        isFound = true;
    }
    if(!isFound) {
        return Result_Broken;
    }

    // VisualSampleEntry: SampleEntry's 8 bytes and its own 70 bytes, then its boxes
    const size_t anEntryEnd = anEntry.Offset + size_t(anEntry.Size);
    size_t aPos = anEntry.Offset + anEntry.HeaderSize + 78;
    if(aPos > anEntryEnd) {
        return Result_Broken;
    }
    size_t anInsertPos = 0;
    for(;;) {
        if(anEntryEnd - aPos < 8) {
            // nothing left, or QuickTime's 32-bit zero terminator: the box goes before it
            for(size_t aTail = aPos; aTail < anEntryEnd; ++aTail) {
                if(theMoov[aTail] != 0) {
                    return Result_Broken;
                }
            }
            if(anInsertPos == 0) {
                anInsertPos = aPos;
            }
            break;
        }
        StMp4Box aChild;
        if(!readBox(theMoov, aPos, anEntryEnd, aChild) || aChild.IsToEnd) {
            return Result_Broken;
        }
        if(aChild.Type == fourCC("st3d")) {
            // Stereoscopic3D extends FullBox('st3d', 0, 0) { unsigned int(8) stereo_mode; }
            if(aChild.Size != 13 || aChild.HeaderSize != 8 || theMoov[aChild.Offset + 8] != 0) {
                return Result_Broken;
            }
            stUByte_t& aModeByte = theMoov[aChild.Offset + 12];
            if(aModeByte == stUByte_t(theMode)) {
                return Result_Unchanged;
            }
            aModeByte = stUByte_t(theMode);
            return Result_Written;
        }
        // st3d goes "before optional boxes at the end of the VisualSampleEntry definition such as the
        // CleanApertureBox and PixelAspectRatioBox", and before sv3d
        if(anInsertPos == 0
        && (aChild.Type == fourCC("clap") || aChild.Type == fourCC("pasp") || aChild.Type == fourCC("sv3d"))) {
            anInsertPos = aPos;
        }
        aPos += size_t(aChild.Size);
    }

    static const size_t THE_ST3D_SIZE = 13;
    for(size_t aBoxIter = 0; aBoxIter < aPath.size(); ++aBoxIter) {
        if(!growBox(theMoov, aPath[aBoxIter], THE_ST3D_SIZE)) {
            return Result_Mp4NoRoom;
        }
    }
    const stUByte_t aSt3d[THE_ST3D_SIZE] = { 0, 0, 0, 13, 's', 't', '3', 'd', 0, 0, 0, 0, stUByte_t(theMode) };
    theMoov.insert(theMoov.begin() + anInsertPos, aSt3d, aSt3d + THE_ST3D_SIZE);
    theGrowth = THE_ST3D_SIZE;
    return Result_Written;
}

bool StVideoStereoMark::readLayout(const StString& thePath,
                                   const int       theStreamIndex,
                                   const bool      theToSkipContainer,
                                   StFormat&       theFormat,
                                   bool&           theIsRightFirst) {
    theFormat = StFormat_AUTO;
    theIsRightFirst = false;
    AVFormatContext* aFormatCtx = NULL;
    if(avformat_open_input(&aFormatCtx, thePath.toCString(), NULL, NULL) < 0) {
        return false;
    }
    if(avformat_find_stream_info(aFormatCtx, NULL) < 0
    || theStreamIndex < 0 || theStreamIndex >= int(aFormatCtx->nb_streams)) {
        avformat_close_input(&aFormatCtx);
        return false;
    }
    for(unsigned int aStreamIter = 0; aStreamIter < aFormatCtx->nb_streams; ++aStreamIter) {
        aFormatCtx->streams[aStreamIter]->discard = int(aStreamIter) == theStreamIndex ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }
    AVStream* aStream = aFormatCtx->streams[theStreamIndex];

    // the container's tag, which the player reads for the layouts FFmpeg gives no side data (the anaglyphs)
    StFormat aTagFormat = StFormat_AUTO;
    bool isTagRightFirst = false;
    StString aTag;
    if(!theToSkipContainer && stAV::meta::readTag(aStream, stCString("STEREO_MODE"), aTag)) {
        aTagFormat = StVideoQueue::formatFromStereoTag(aTag, isTagRightFirst);
    }

    const AVCodec*  aCodec    = avcodec_find_decoder(aStream->codecpar->codec_id);
    AVCodecContext* aCodecCtx = aCodec != NULL ? avcodec_alloc_context3(aCodec) : NULL;
    bool isDecoded = false;
    if(aCodecCtx != NULL
    && avcodec_parameters_to_context(aCodecCtx, aStream->codecpar) >= 0) {
    #if(LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 29, 100))
        if(theToSkipContainer) {
            // the decoder adds the container's declaration to each picture: leave it out
            av_packet_side_data_remove(aCodecCtx->coded_side_data, &aCodecCtx->nb_coded_side_data, AV_PKT_DATA_STEREO3D);
        }
    #endif
        aCodecCtx->thread_count = 1;
        if(avcodec_open2(aCodecCtx, aCodec, NULL) >= 0) {
            AVPacket* aPacket = av_packet_alloc();
            AVFrame*  aFrame  = av_frame_alloc();
            bool isEof = false;
            for(int aPacketIter = 0; aPacketIter < 1000 && !isDecoded; ++aPacketIter) {
                if(!isEof && av_read_frame(aFormatCtx, aPacket) < 0) {
                    isEof = true;
                    avcodec_send_packet(aCodecCtx, NULL);
                } else if(!isEof) {
                    const bool isOurs = aPacket->stream_index == theStreamIndex;
                    if(isOurs) {
                        avcodec_send_packet(aCodecCtx, aPacket);
                    }
                    av_packet_unref(aPacket);
                    if(!isOurs) {
                        continue;
                    }
                }
                const int aRes = avcodec_receive_frame(aCodecCtx, aFrame);
                if(aRes == 0) {
                    isDecoded = true;
                    if(const AVFrameSideData* aSideData = av_frame_get_side_data(aFrame, AV_FRAME_DATA_STEREO3D)) {
                        const AVStereo3D* aStereo = (const AVStereo3D* )aSideData->data;
                        theFormat = stAV::stereo3dAvToSt(aStereo->type);
                        if(aStereo->flags & AV_STEREO3D_FLAG_INVERT) {
                            theFormat = st::formatReversed(theFormat);
                        }
                        theIsRightFirst = theFormat == StFormat_FrameSequence && (aStereo->flags & AV_STEREO3D_FLAG_INVERT) != 0;
                    }
                } else if(aRes == AVERROR_EOF || (isEof && aRes == AVERROR(EAGAIN))) {
                    break;
                }
            }
            av_frame_free(&aFrame);
            av_packet_free(&aPacket);
        }
    }
    if(aCodecCtx != NULL) {
        avcodec_free_context(&aCodecCtx);
    }

    if(!theToSkipContainer && !isDecoded) {
        // no picture: the container's declaration, as the decoder would have added it
    #if(LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 29, 100))
        const AVPacketSideData* aSideData = av_packet_side_data_get(aStream->codecpar->coded_side_data,
                                                                    aStream->codecpar->nb_coded_side_data,
                                                                    AV_PKT_DATA_STEREO3D);
        if(aSideData != NULL) {
            const AVStereo3D* aStereo = (const AVStereo3D* )aSideData->data;
            theFormat = stAV::stereo3dAvToSt(aStereo->type);
            if(aStereo->flags & AV_STEREO3D_FLAG_INVERT) {
                theFormat = st::formatReversed(theFormat);
            }
            theIsRightFirst = theFormat == StFormat_FrameSequence && (aStereo->flags & AV_STEREO3D_FLAG_INVERT) != 0;
        }
    #endif
    }
    if(theFormat == StFormat_AUTO && aTagFormat != StFormat_AUTO) {
        theFormat = aTagFormat;
        theIsRightFirst = isTagRightFirst;
    }
    avformat_close_input(&aFormatCtx);
    return true;
}

#if defined(__linux__) && !defined(__ANDROID__)
namespace {

    static bool readAt(const int theFd, const off_t theOffset, stUByte_t* theData, const size_t theSize) {
        size_t aDone = 0;
        while(aDone < theSize) {
            const ssize_t aRes = ::pread(theFd, theData + aDone, theSize - aDone, theOffset + off_t(aDone));
            if(aRes <= 0) {
                if(aRes < 0 && errno == EINTR) {
                    continue;
                }
                return false;
            }
            aDone += size_t(aRes);
        }
        return true;
    }

    static bool writeAt(const int theFd, const off_t theOffset, const stUByte_t* theData, const size_t theSize) {
        size_t aDone = 0;
        while(aDone < theSize) {
            const ssize_t aRes = ::pwrite(theFd, theData + aDone, theSize - aDone, theOffset + off_t(aDone));
            if(aRes <= 0) {
                if(aRes < 0 && errno == EINTR) {
                    continue;
                }
                return false;
            }
            aDone += size_t(aRes);
        }
        return true;
    }

    /**
     * Write st3d into the file's moov box, in place: the box keeps its offset, so every chunk offset
     * of the media data stays valid. A moov box that grows must be the file's last box or be followed
     * by a free space box ('free' or 'skip') with room for the 13 bytes.
     */
    static StVideoStereoMark::Result writeMp4(const StString& thePath, const uint32_t theTrackId, const int theMode,
                                              StString& theDetails) {
        const int aFd = ::open(thePath.toCString(), O_RDWR);
        if(aFd < 0) {
            theDetails = StString(strerror(errno));
            return StVideoStereoMark::Result_WriteFailed;
        }
        struct stat aStat;
        if(::fstat(aFd, &aStat) != 0) {
            theDetails = StString(strerror(errno));
            ::close(aFd);
            return StVideoStereoMark::Result_WriteFailed;
        }
        const off_t aFileSize = aStat.st_size;

        // the top-level boxes: find moov, and what follows it
        off_t aMoovOffset = -1, aMoovSize = 0, aNextOffset = -1, aNextSize = 0;
        uint32_t aNextType = 0;
        size_t aNextHeader = 0;
        for(off_t aPos = 0; aPos < aFileSize;) {
            stUByte_t aHeader[16];
            if(aFileSize - aPos < 8 || !readAt(aFd, aPos, aHeader, 8)) {
                break;
            }
            uint64_t aSize = readU32BE(aHeader);
            const uint32_t aType = readU32BE(aHeader + 4);
            size_t aHeaderSize = 8;
            if(aSize == 1) {
                if(aFileSize - aPos < 16 || !readAt(aFd, aPos + 8, aHeader + 8, 8)) {
                    break;
                }
                aSize = readU64BE(aHeader + 8);
                aHeaderSize = 16;
            } else if(aSize == 0) {
                aSize = uint64_t(aFileSize - aPos);
            }
            if(aSize < aHeaderSize || aSize > uint64_t(aFileSize - aPos)) {
                break;
            }
            if(aMoovOffset >= 0 && aNextOffset < 0) {
                aNextOffset = aPos;
                aNextSize   = off_t(aSize);
                aNextType   = aType;
                aNextHeader = aHeaderSize;
            }
            if(aType == fourCC("moov")) {
                if(aMoovOffset >= 0) {
                    ::close(aFd);
                    return StVideoStereoMark::Result_Broken;
                }
                aMoovOffset = aPos;
                aMoovSize   = off_t(aSize);
            }
            aPos += off_t(aSize);
        }
        if(aMoovOffset < 0 || aMoovSize > off_t(256 * 1024 * 1024)) {
            ::close(aFd);
            return StVideoStereoMark::Result_Broken;
        }

        std::vector<stUByte_t> aMoov;
        aMoov.resize(size_t(aMoovSize));
        if(!readAt(aFd, aMoovOffset, aMoov.data(), aMoov.size())) {
            theDetails = StString(strerror(errno));
            ::close(aFd);
            return StVideoStereoMark::Result_WriteFailed;
        }
        size_t aGrowth = 0;
        const StVideoStereoMark::Result aResult = StVideoStereoMark::setupMp4Moov(aMoov, theTrackId, theMode, aGrowth);
        if(aResult != StVideoStereoMark::Result_Written) {
            ::close(aFd);
            return aResult;
        }

        // what is written over: the moov box, and the start of the free space box that gives up its bytes
        std::vector<stUByte_t> aRegion = aMoov;
        if(aGrowth != 0) {
            const bool isLast = aMoovOffset + aMoovSize == aFileSize;
            const bool isFree = aNextOffset == aMoovOffset + aMoovSize
                             && (aNextType == fourCC("free") || aNextType == fourCC("skip"))
                             && aNextHeader == 8
                             && (aNextSize == off_t(aGrowth) || aNextSize >= off_t(aGrowth + 8));
            if(!isLast && !isFree) {
                ::close(aFd);
                return StVideoStereoMark::Result_Mp4NoRoom;
            }
            if(!isLast && aNextSize != off_t(aGrowth)) {
                // the free space box, shorter by the bytes st3d takes; its content is not read
                stUByte_t aFree[8] = { 0, 0, 0, 0, 'f', 'r', 'e', 'e' };
                writeU32BE(aFree, uint32_t(aNextSize - off_t(aGrowth)));
                aRegion.insert(aRegion.end(), aFree, aFree + 8);
            }
        }

        bool isWritten = writeAt(aFd, aMoovOffset, aRegion.data(), aRegion.size()) && ::fsync(aFd) == 0;
        std::vector<stUByte_t> aRead(aRegion.size());
        isWritten = isWritten && readAt(aFd, aMoovOffset, aRead.data(), aRead.size()) && aRead == aRegion;
        if(!isWritten && errno == 0) { errno = EIO; }
        const int anError = errno;
        isWritten = ::close(aFd) == 0 && isWritten;
        if(!isWritten) {
            theDetails = StString(strerror(anError));
            return StVideoStereoMark::Result_WriteFailed;
        }
        return StVideoStereoMark::Result_Written;
    }

    static StVideoStereoMark::Result runMkvpropedit(const StString& thePath, const int theVideoTrack, const int theValue,
                                                    StString& theDetails) {
        std::vector<StString> anArgs;
        anArgs.push_back(thePath);
        anArgs.push_back(stCString("--edit"));
        anArgs.push_back(StString("track:v") + theVideoTrack);
        if(theValue < 0) {
            anArgs.push_back(stCString("--delete"));
            anArgs.push_back(stCString("stereo-mode"));
        } else {
            anArgs.push_back(stCString("--set"));
            anArgs.push_back(StString("stereo-mode=") + theValue);
        }
        int anExitCode = -1;
        StString anOutput, anError;
        if(!StProcess::runProcessInPath(stCString("mkvpropedit"), anArgs, anExitCode, anOutput, anError)) {
            theDetails = anError;
            return anError == StString(strerror(ENOENT))
                 ? StVideoStereoMark::Result_ToolMissing
                 : StVideoStereoMark::Result_WriteFailed;
        }
        // mkvpropedit exits with 0 on success, 1 with warnings, 2 on errors
        if(anExitCode != 0 && anExitCode != 1) {
            theDetails = anOutput;
            return StVideoStereoMark::Result_WriteFailed;
        }
        return StVideoStereoMark::Result_Written;
    }

}
#endif

StVideoStereoMark::Result StVideoStereoMark::save(const Request& theRequest,
                                                  StFormat&      theDeclared,
                                                  StString&      theDetails) {
    theDeclared = StFormat_AUTO;
    theDetails.clear();
#if defined(__linux__) && !defined(__ANDROID__)
    struct stat aStat;
    if(theRequest.Path.isEmpty()
    || ::stat(theRequest.Path.toCString(), &aStat) != 0
    || !S_ISREG(aStat.st_mode)) {
        return Result_NotLocal;
    }

    const Container aContainer = containerOf(theRequest.FormatName, theRequest.Path);
    int aValue = -1;
    switch(aContainer) {
        case Container_Matroska:
        case Container_WebM:
            aValue = matroskaStereoMode(theRequest.Format, theRequest.IsRightFirst, aContainer == Container_WebM);
            break;
        case Container_Mp4:
            aValue = mp4StereoMode(theRequest.Format);
            break;
        case Container_Other:
        default:
            return Result_Container;
    }
    if(aValue < 0) {
        return Result_Layout;
    }

    // a declaration in the video stream itself (the frame packing SEI) comes before the container's
    StFormat aStreamFormat = StFormat_AUTO;
    bool isStreamRightFirst = false;
    if(readLayout(theRequest.Path, theRequest.StreamIndex, true, aStreamFormat, isStreamRightFirst)
    && aStreamFormat != StFormat_AUTO
    && !isSameLayout(aStreamFormat, isStreamRightFirst, theRequest.Format, theRequest.IsRightFirst)) {
        theDeclared = aStreamFormat;
        return Result_StreamDeclares;
    }

    StFormat aFormat = StFormat_AUTO;
    bool isRightFirst = false;
    if(readLayout(theRequest.Path, theRequest.StreamIndex, false, aFormat, isRightFirst)
    && isSameLayout(aFormat, isRightFirst, theRequest.Format, theRequest.IsRightFirst)
    && aFormat != StFormat_AUTO) {
        return Result_Unchanged;
    }

    std::vector<stUByte_t> anOriginalMedia;
    if(!mediaDigest(theRequest.Path, anOriginalMedia)) { return Result_Broken; }
    StFileTransaction aTransaction(theRequest.Path, false);
    if(!aTransaction.isValid()) {
        theDetails = aTransaction.error();
        return Result_WriteFailed;
    }
    const StString& aTemporary = aTransaction.temporaryPath();
    Result aResult = Result_Broken;
    if(aContainer == Container_Mp4) {
        AVFormatContext* aCtx = NULL;
        int aTrackId = -1;
        if(avformat_open_input(&aCtx, theRequest.Path.toCString(), NULL, NULL) >= 0) {
            if(theRequest.StreamIndex >= 0 && theRequest.StreamIndex < int(aCtx->nb_streams)) {
                // FFmpeg's mov demuxer gives each stream the track_ID of its track header
                aTrackId = aCtx->streams[theRequest.StreamIndex]->id;
            }
            avformat_close_input(&aCtx);
        }
        if(aTrackId <= 0) {
            return Result_Broken;
        }
        aResult = writeMp4(aTemporary, uint32_t(aTrackId), aValue, theDetails);
        if(aResult == Result_Unchanged) {
            return Result_Unchanged;
        }
    } else {
        aResult = runMkvpropedit(aTemporary, theRequest.VideoTrack, aValue, theDetails);
    }
    if(aResult != Result_Written) {
        return aResult;
    }

    // Only a verified temporary copy may replace the person's file.
    if(!readLayout(aTemporary, theRequest.StreamIndex, false, aFormat, isRightFirst)
    || !isSameLayout(aFormat, isRightFirst, theRequest.Format, theRequest.IsRightFirst)) {
        theDeclared = aFormat;
        return Result_ReadBack;
    }
    std::vector<stUByte_t> aSavedMedia;
    if(!mediaDigest(aTemporary, aSavedMedia) || aSavedMedia != anOriginalMedia) { return Result_ReadBack; }
    if(!aTransaction.seal()) { return Result_ReadBack; }
    if(!aTransaction.commit()) {
        theDetails = aTransaction.error();
        return Result_WriteFailed;
    }
    return Result_Written;
#else
    (void )theRequest;
    return Result_Container;
#endif
}
