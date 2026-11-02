// The 3D layout of a video written into its container's own mark: Matroska and WebM StereoMode
// (RFC 9559 5.1.4.1.28.3, written with MKVToolNix's mkvpropedit), MP4 and MOV st3d (Spherical Video V2 RFC).
// MIT license; see docs/license-mit.txt.

#ifndef __StVideoStereoMark_h_
#define __StVideoStereoMark_h_

#include <StGLStereo/StFormatEnum.h>

#include <vector>

class StVideoStereoMark {

        public:

    enum Container {
        Container_Other,
        Container_Matroska,
        Container_WebM,
        Container_Mp4,     //!< ISO base media files: MP4, MOV and their relatives
    };

    enum Result {
        Result_Written,        //!< the file carries the layout now
        Result_Unchanged,      //!< the file carried the layout already
        Result_NotLocal,       //!< not a local file
        Result_Container,      //!< the container has no 3D mark sView writes
        Result_Layout,         //!< the container's mark has no value for the layout
        Result_Mp4NoRoom,      //!< st3d cannot be added without moving the media data
        Result_Mp4Entries,     //!< the track has several sample descriptions
        Result_Broken,         //!< the file's structure could not be read
        Result_ToolMissing,    //!< mkvpropedit is not installed
        Result_WriteFailed,    //!< writing failed; the details say why
        Result_StreamDeclares, //!< the video stream declares another layout itself, and it comes first
        Result_ReadBack,       //!< the written file did not read back with the layout; it was restored
    };

    /**
     * What to write: the file, the video stream (FFmpeg's index), its place among the file's video tracks
     * (1 for the first, as mkvpropedit's track:v1 counts) and the layout.
     */
    struct Request {
        StString Path;
        StString FormatName;     //!< FFmpeg's demuxer name
        int      StreamIndex;
        int      VideoTrack;
        StFormat Format;
        bool     IsRightFirst;   //!< a frame sequence that starts each pair with the right view

        Request() : StreamIndex(-1), VideoTrack(1), Format(StFormat_AUTO), IsRightFirst(false) {}
    };

    /**
     * The container family of the file (the EBML DocType tells WebM from Matroska).
     */
    ST_LOCAL static Container containerOf(const StString& theFormatName,
                                          const StString& thePath);

    /**
     * Matroska StereoMode value for the layout, -1 when Table 5 of RFC 9559 has none;
     * WebM allows only 0, 1, 2, 3 and 11 (WebM Container Guidelines, StereoMode).
     */
    ST_LOCAL static int matroskaStereoMode(const StFormat theFormat,
                                           const bool     theIsRightFirst,
                                           const bool     theIsWebM);

    /**
     * st3d stereo_mode for the layout, -1 for a layout it has no value for,
     * or that FFmpeg (which reads it for sView) does not read.
     */
    ST_LOCAL static int mp4StereoMode(const StFormat theFormat);

    /**
     * Put st3d with the given mode into the first visual sample entry of the track: the mode byte is replaced
     * when the box exists, else the box is inserted before the optional boxes that end the entry
     * (clap, pasp, sv3d) and every enclosing box grows by its 13 bytes.
     * @param theMoov    the whole moov box (from its size field)
     * @param theTrackId track_ID of the track header
     * @param theMode    st3d stereo_mode
     * @param theGrowth  [out] bytes added to the moov box (0 or 13)
     * @return Result_Written, Result_Unchanged, Result_Mp4Entries or Result_Broken
     */
    ST_LOCAL static Result setupMp4Moov(std::vector<stUByte_t>& theMoov,
                                        const uint32_t          theTrackId,
                                        const int               theMode,
                                        size_t&                 theGrowth);

    /**
     * Write the layout into the file, when the person asked for it.
     * @param theRequest  the file, the stream and the layout
     * @param theDeclared [out] the layout the stream declares itself, for Result_StreamDeclares
     * @param theDetails  [out] why writing failed, for Result_WriteFailed and Result_Broken
     */
    ST_LOCAL static Result save(const Request& theRequest,
                                StFormat&      theDeclared,
                                StString&      theDetails);

    /**
     * Read the layout the way sView's player reads it: the first decoded picture's side data (the stream's
     * own declaration, completed by the container's), then the container's mark.
     * @param theToSkipContainer read only what the stream itself declares
     */
    ST_LOCAL static bool readLayout(const StString& thePath,
                                    const int       theStreamIndex,
                                    const bool      theToSkipContainer,
                                    StFormat&       theFormat,
                                    bool&           theIsRightFirst);

};

#endif // __StVideoStereoMark_h_
