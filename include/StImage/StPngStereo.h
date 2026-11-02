// PNG sTER chunk: the stereoscopic layout written into a PNG datastream, every other byte kept.
// MIT license; see docs/license-mit.txt.

#ifndef __StPngStereo_h_
#define __StPngStereo_h_

#include <StGLStereo/StFormatEnum.h>

#include <vector>

/**
 * Writes the stereoscopic layout of a PNG datastream into its sTER chunk
 * (Extensions to the PNG 1.2 Specification, version 1.4.0, section 3.6), keeping every other byte.
 */
class StPngStereo {

        public:

    enum Result {
        Result_Written,    //!< the new datastream carries the layout
        Result_Unchanged,  //!< the datastream already carries the layout, nothing to write
        Result_NotPng,     //!< not a PNG datastream with IHDR first and image data
        Result_Layout,     //!< sTER describes only side by side pairs (mode 0 cross-fuse, mode 1 diverging-fuse)
        Result_Width,      //!< the right view would not start on a column divisible by eight
        Result_MonoNoMark, //!< a picture without sTER is already one picture; PNG has no other 2D mark
    };

    /**
     * Prepare the datastream carrying the given layout: one sTER chunk right after IHDR (side by side),
     * or no sTER chunk (mono).
     * The chunk describes the picture as it is only when its width W puts the right view on a column divisible
     * by eight without padding: the decoder's padding 15 - ((W - 1) mod 16) must be 0.
     * @param theData      PNG datastream
     * @param theSize      its size in bytes
     * @param theFormat    layout to write
     * @param theResult    [out] the new datastream for Result_Written
     * @param theWidth     [out] the image width read from IHDR
     */
    ST_CPPEXPORT static Result setupSter(const stUByte_t*        theData,
                                         const size_t            theSize,
                                         const StFormat          theFormat,
                                         std::vector<stUByte_t>& theResult,
                                         uint32_t&               theWidth);

    /**
     * Return the layout declared by the sTER chunk, StFormat_AUTO when there is none.
     */
    ST_CPPEXPORT static StFormat readSter(const stUByte_t* theData,
                                          const size_t     theSize);

    /**
     * CRC of a chunk (PNG specification, Annex D): over the chunk type and data.
     */
    ST_CPPEXPORT static uint32_t chunkCrc(const stUByte_t* theTypeAndData,
                                          const size_t     theSize);

};

#endif // __StPngStereo_h_
