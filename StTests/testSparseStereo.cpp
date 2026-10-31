// Sparse eye grids are compacted from each eye's own samples; nothing is interpolated.
// MIT license; see docs/license-mit.txt.
#include <StGLStereo/StGLTextureData.h>
#include <StAV/stAV.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

int main() {
    int failures = 0, cases = 0;
    for (StFormat format : {StFormat_Checkerboard, StFormat_Checkerboard_RL,
                           StFormat_Quincunx, StFormat_Quincunx_RL, StFormat_Columns, StFormat_Columns_RL}) {
        for (bool topDown : {false, true}) {
            for (StImagePlane::ImgFormat pixel : {StImagePlane::ImgGray, StImagePlane::ImgGray16, StImagePlane::ImgGrayF}) {
                StImage packed, empty, eyes[2];
                const bool columns = format == StFormat_Columns || format == StFormat_Columns_RL;
                const bool parallel = format == StFormat_Quincunx || format == StFormat_Quincunx_RL;
                const bool reverse = format == StFormat_Checkerboard_RL || format == StFormat_Quincunx_RL || format == StFormat_Columns_RL;
                for (size_t plane = 0; plane < 3; ++plane) {
                    const size_t w = plane ? 8 : 16, h = plane ? 4 : 8;
                    StImagePlane& p = packed.changePlane(plane);
                    p.initTrash(pixel, w, h, w * (pixel == StImagePlane::ImgGray ? 1 : pixel == StImagePlane::ImgGray16 ? 2 : 4) + 8);
                    p.setTopDown(topDown);
                    // each sample carries its eye and its column in that eye's compact view
                    for (size_t y = 0; y < h; ++y) for (size_t x = 0; x < w; ++x) {
                        const size_t eye = columns ? x % 2 : parallel ? x / (w / 2) : (x + y) % 2;
                        const size_t ex = parallel ? x % (w / 2) : x / 2;
                        const float v = float(eye * 100 + plane * 10 + ex * 2 + y * 4);
                        GLubyte* d = p.changeData(topDown ? y : h - 1 - y, x);
                        if (pixel == StImagePlane::ImgGray) *d = GLubyte(v);
                        else if (pixel == StImagePlane::ImgGray16) { const uint16_t n = uint16_t(v * 257); std::memcpy(d, &n, 2); }
                        else std::memcpy(d, &v, 4);
                    }
                }
                StGLTextureData data(std::make_shared<StGLTextureUploadParams>());
                data.updateData(StGLDeviceCaps(), packed, empty, std::make_shared<StStereoParams>(), format, StCubemap_OFF, 0.0);
                data.getCopy(&eyes[0], &eyes[1]);
                bool ok = true;
                for (size_t eye = 0; eye < 2; ++eye) for (size_t plane = 0; plane < 3; ++plane) {
                    // every eye is half the packed width; each of its samples is the source's own
                    const size_t w = (plane ? 8 : 16) / 2, h = plane ? 4 : 8, sourceEye = eye ^ reverse;
                    const StImagePlane& p = eyes[eye].getPlane(plane);
                    if (p.getSizeX() != w || p.getSizeY() != h) { ok = false; continue; }
                    for (size_t y = 0; y < h; ++y) for (size_t x = 0; x < w; ++x) {
                        const float expected = float(sourceEye * 100 + plane * 10 + x * 2 + y * 4);
                        const GLubyte* d = p.getData(y, x);
                        if (pixel == StImagePlane::ImgGray) ok = ok && *d == GLubyte(expected);
                        else if (pixel == StImagePlane::ImgGray16) { uint16_t n; std::memcpy(&n, d, 2); ok = ok && n == uint16_t(expected * 257); }
                        else { float n; std::memcpy(&n, d, 4); ok = ok && n == expected; }
                    }
                }
                ++cases; failures += !ok;
                std::printf("sparse format=%d topDown=%d pixel=%d %s\n", int(format), int(topDown), int(pixel), ok ? "PASS" : "FAIL");
            }
        }
    }
    std::printf("%d cases, %d failures\n", cases, failures);
    return failures != 0;
}
