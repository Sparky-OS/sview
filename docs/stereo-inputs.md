# Packed stereo inputs

Every packing is cut into its two views, and each view keeps the samples it was given: nothing is stretched, squeezed or interpolated.
Stereo KWin brings each view to the screen's size once, at the output.

Columns are separated into two half-width sample planes.
Their pixel aspect ratio doubles, so their display shape is preserved without expanding their raster.
Rows use the corresponding half-height planes and pixel aspect ratio.

Checkerboard (H.264 D.2.26, frame_packing_arrangement_type 0) is compacted the same way as columns.
The first view takes the samples whose row and column add up to an even number, the second view the others, so each row of a view starts one sample further on than the row above it would in the packed frame.
Each view is half the width of the frame, with a doubled pixel aspect ratio; the samples of the other view are never used to fill it.
Side by side with quincunx sampling (frame_packing_arrangement_type 3 with quincunx_sampling_flag, FFmpeg's AV_STEREO3D_SIDEBYSIDE_QUINCUNX) is shown as side by side: each half already holds its own view's samples.
Reversed layouts exchange the views.

RGB and 4:4:4 pictures are copied sample for sample.
In 4:2:0 the luma is copied exactly and each chroma sample covers both views, so the nearest one is taken.

Ordinary half side-by-side and half top-and-bottom inputs retain their smaller rasters.

See [ITU-T H.264 (08/2024)](https://www.itu.int/rec/T-REC-H.264-202408-I/en), D.2.26 and Figures D-1 to D-11.
The FFmpeg input enumeration is documented in [libavutil/stereo3d.h](https://ffmpeg.org/doxygen/trunk/stereo3d_8h_source.html).

The CPU tests use address-coded samples, both eye orders, padded rows, both row orientations, and three planes at luma and chroma resolutions.
They require every sample of a view to be that view's own transmitted sample, with no contribution from the other view.
