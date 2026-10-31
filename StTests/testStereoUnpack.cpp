// Byte-exact eye separation, independent of OpenGL and of the upload path.
// MIT license; see docs/license-mit.txt.
#include <StGLStereo/StGLTextureData.h>
#include <StAV/stAV.h>
#include <cstdio>

int main() {
    const AVStereo3DType aTypes[] = {AV_STEREO3D_COLUMNS, AV_STEREO3D_CHECKERBOARD,
        AV_STEREO3D_SIDEBYSIDE_QUINCUNX, AV_STEREO3D_SIDEBYSIDE, AV_STEREO3D_LINES,
        AV_STEREO3D_TOPBOTTOM};
    int aFailures = 0;
    for (AVStereo3DType aType : aTypes) {
        for (bool isTopDown : {false, true}) {
            StImage aPacked, anEmpty, anEyes[2];
            StImagePlane& aPlane = aPacked.changePlane(0);
            aPlane.initTrash(StImagePlane::ImgRGB, 16, 8, 52);
            aPlane.setTopDown(isTopDown);
            const bool isVertical = aType == AV_STEREO3D_LINES || aType == AV_STEREO3D_TOPBOTTOM;
            // every eye keeps its own samples only: half the width, or half the height
            const size_t aWidth = isVertical ? 16 : 8, aHeight = isVertical ? 4 : 8;
            // Encode each sample with its eye and coordinates in that eye.
            for (size_t y = 0; y < 8; ++y) {
                for (size_t x = 0; x < 16; ++x) {
                    size_t eye, ex, ey = y;
                    if (aType == AV_STEREO3D_COLUMNS) { eye = x % 2; ex = x / 2; }
                    else if (aType == AV_STEREO3D_CHECKERBOARD) { eye = (x + y) % 2; ex = x / 2; }
                    else if (aType == AV_STEREO3D_LINES) { eye = y % 2; ex = x; ey = y / 2; }
                    else if (aType == AV_STEREO3D_TOPBOTTOM) { eye = y / 4; ex = x; ey = y % 4; }
                    else { eye = x / 8; ex = x % 8; }
                    GLubyte* p = aPlane.changeData(isTopDown ? y : 7 - y, x);
                    p[0] = eye ? 203 : 37; p[1] = GLubyte(ex + 10); p[2] = GLubyte(ey + 20);
                }
            }
            StGLTextureData aData(std::make_shared<StGLTextureUploadParams>());
            const StFormat aFormat = stAV::stereo3dAvToSt(aType);
            aData.updateData(StGLDeviceCaps(), aPacked, anEmpty,
                std::make_shared<StStereoParams>(), aFormat, StCubemap_OFF, 0.0);
            aData.getCopy(&anEyes[0], &anEyes[1]);
            bool isGood = aFormat != StFormat_AUTO;
            for (size_t eye = 0; eye < 2; ++eye) {
                const StImagePlane& p = anEyes[eye].getPlane(0);
                if (p.isNull() || p.getSizeX() != aWidth || p.getSizeY() != aHeight) { isGood = false; continue; }
                for (size_t y = 0; y < aHeight; ++y) {
                    for (size_t x = 0; x < aWidth; ++x) {
                        const GLubyte* v = p.getData(y, x);
                        isGood = isGood && v[0] == (eye ? 203 : 37) && v[1] == x + 10 && v[2] == y + 20;
                    }
                }
            }
            std::printf("type=%d topDown=%d %s\n", int(aType), int(isTopDown), isGood ? "PASS" : "FAIL");
            aFailures += !isGood;
        }
    }
    return aFailures ? 1 : 0;
}
