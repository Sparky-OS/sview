// Exercise the actual loader's default eye order and accepted save without a GL context.
// MIT license; see docs/license-mit.txt.
#include "../StImageViewer/StImageLoader.h"
#include "../StImageViewer/StImageViewerStrings.h"
#include <StImage/StPictureCorrection.h>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <unistd.h>

struct StMpoLoaderTest {
    static bool equal(const StImage& a, const StImage& b) {
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
    static int run(const StString& path) {
        auto resource = std::make_shared<StResourceManager>("sview-test");
        auto messages = std::make_shared<StMsgQueue>();
        auto language = std::make_shared<StLangMap>();
        StImageViewerStrings::loadDefaults(*language);
        auto playlist = std::make_shared<StPlayList>(0);
        auto queue = std::make_shared<StGLTextureQueue>(4);
        playlist->addOneFile(path, StMIME());
        std::shared_ptr<StFileNode> node;
        std::shared_ptr<StStereoParams> params;
        if(!playlist->getCurrentFile(node, params)) { return 2; }
        StImageLoader loader(StImageFile::ST_LIBAV, resource, messages, language, playlist, queue, 4096);
        loader.params.ToSwapJPS = new StBoolParam(false);
        loader.params.ToSaveCrossEyed = new StBoolParam(false);
        loader.params.ToStickPanorama = new StBoolParam(false);
        loader.params.LastPanoramaMode = new StInt32Param(0);
        loader.params.ToFlipCubeZ6x1 = new StBoolParam(false);
        loader.params.ToFlipCubeZ3x2 = new StBoolParam(false);
        if(!loader.loadImage(node, params) || !loader.myImgInfo->IsSavable) { return 3; }
        StImage beforeLeft, beforeRight;
        queue->myDataFront->getCopy(&beforeLeft, &beforeRight);
        if(beforeLeft.isNull() || beforeRight.isNull() || equal(beforeLeft, beforeRight)) { return 4; }
        params->InputFormat = StFormat_SeparateFrames;
        // The renderer normally publishes this after showing the queued pair.
        params->StereoFormat = StFormat(queue->getSrcFormat());
        params->setSwapLR(true);
        if(!loader.saveImageInfo(loader.myImgInfo)) {
            StMsg message;
            while(messages->pop(message)) { std::printf("save refusal: %s\n", message.Text->toCString()); }
            return 5;
        }
        StPictureCorrection::clearSaved(*params, true);
        if(params->InputFormat != StFormat_AUTO || params->ToSwapLR || !loader.loadImage(node, params)) { return 6; }
        StImage afterLeft, afterRight;
        queue->myDataFront->getCopy(&afterLeft, &afterRight);
        if(!equal(afterLeft, beforeRight) || !equal(afterRight, beforeLeft)) { return 7; }
        std::printf("PASS default MPO reload gives the saved eyes with both per-file choices cleared\n");
        return 0;
    }
};
int main() {
    char name[] = "loader-mpo-XXXXXX";
    int fd = mkstemp(name);
    if(fd < 0) { return 2; }
    close(fd);
    const std::string path = std::string(name) + ".mpo";
    if(rename(name, path.c_str())) { return 2; }
    std::ifstream source(std::string(CORRECTION_DATA) + "/pair.mpo", std::ios::binary);
    std::ofstream target(path, std::ios::binary);
    target << source.rdbuf();
    target.close();
    const int result = StMpoLoaderTest::run(StString(path.c_str()));
    unlink(path.c_str());
    std::printf("MPO loader result=%d\n", result);
    return result;
}
