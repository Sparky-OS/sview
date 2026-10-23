# Packed stereo inputs

Columns are separated into two half-width sample planes.
Their pixel aspect ratio doubles, so their display shape is preserved without expanding their raster.
Rows use the corresponding half-height planes and pixel aspect ratio.

Checkerboard and side-by-side quincunx contain complementary sparse sample grids.
Separating each row into a compact rectangle loses the alternating horizontal phase.
The decoder therefore places each transmitted sample at its grid position and fills missing positions with the mean of the available orthogonal neighbours from that same eye.
It preserves transmitted samples exactly, handles each colour plane independently, and supports byte, 16-bit and floating-point samples.
Reversed layouts exchange the completed eyes.
This reconstructs a sparse input grid; ordinary half side-by-side and half top-and-bottom inputs retain their smaller rasters.

The grid interpretation follows H.264 D.2.26, Figures D-1 and D-10.
That clause leaves the reconstruction method to the implementation and describes the neighbour mean as an example.
See [ITU-T H.264 (08/2024)](https://www.itu.int/rec/T-REC-H.264-202408-I/en).
The FFmpeg input enumeration is documented in [libavutil/stereo3d.h](https://ffmpeg.org/doxygen/trunk/stereo3d_8h_source.html).

The CPU tests use address-coded samples and affine fields, both eye orders, padded rows, both row orientations, and three planes at luma and chroma resolutions.
They require exact transmitted samples and exact interior reconstruction, with no cross-eye contributions.
