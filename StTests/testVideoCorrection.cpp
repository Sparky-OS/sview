// The real container writer, callable without opening a window.
// MIT license; see docs/license-mit.txt.
#include "../StMoviePlayer/StVideo/StVideoStereoMark.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    if(argc != 7) { return 100; }
    StVideoStereoMark::Request request;
    request.Path = StString(argv[1]);
    request.FormatName = StString(argv[2]);
    request.Format = StFormat(std::atoi(argv[3]));
    request.IsRightFirst = std::atoi(argv[4]) != 0;
    request.StreamIndex = std::atoi(argv[5]);
    request.VideoTrack = std::atoi(argv[6]);
    StFormat declared = StFormat_AUTO;
    StString details;
    const auto result = StVideoStereoMark::save(request, declared, details);
    std::printf("result=%d declared=%d details=%s\n", int(result), int(declared), details.toCString());
    return int(result);
}
