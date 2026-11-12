// Metadata-only correction and verified replacement, without a display server.
// MIT license; see docs/license-mit.txt.
#include <StFile/StFileTransaction.h>
#include <StImage/StPictureCorrection.h>
#include <StGL/StParams.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static int THE_FAILURES = 0;
static void check(bool theOk, const char* theName) {
    std::printf("%s %s\n", theOk ? "PASS" : "FAIL", theName);
    std::fflush(stdout);
    THE_FAILURES += !theOk;
}
static std::vector<stUByte_t> readFile(const std::string& thePath) {
    FILE* f = std::fopen(thePath.c_str(), "rb");
    if(!f || std::fseek(f, 0, SEEK_END)) { std::exit(2); }
    const long n = std::ftell(f);
    if(n < 0 || std::fseek(f, 0, SEEK_SET)) { std::exit(2); }
    std::vector<stUByte_t> bytes(size_t(n), 0);
    if(std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size() || std::fclose(f)) { std::exit(2); }
    return bytes;
}
static void writeFile(const std::string& thePath, const std::vector<stUByte_t>& theBytes) {
    FILE* f = std::fopen(thePath.c_str(), "wb");
    if(!f || std::fwrite(theBytes.data(), 1, theBytes.size(), f) != theBytes.size() || std::fclose(f)) { std::exit(2); }
}
int main() {
    struct StateCase { StFormat Format; bool CarriesOrder; bool ClearSwap; };
    const StateCase aStates[] = {
        {StFormat_SideBySide_LR, false, true}, {StFormat_TopBottom_RL, false, true},
        {StFormat_Rows_RL, false, true}, {StFormat_AnaglyphRedCyan, false, false},
        {StFormat_SeparateFrames, true, true}, {StFormat_FrameSequence, true, true}
    };
    for(const auto& state : aStates) {
        StStereoParams params;
        params.StereoFormat = params.InputFormat = state.Format;
        params.setSwapLR(true);
        StPictureCorrection::clearSaved(params, state.CarriesOrder);
        check(params.InputFormat == StFormat_AUTO && params.ToSwapLR == !state.ClearSwap, "saved per-file choices unmarked only when represented");
    }
    char aTemplate[] = "correction-save-XXXXXX";
    char* aDirectory = mkdtemp(aTemplate);
    if(!aDirectory) { return 2; }
    const char* aNames[] = {"pair.jpg", "progressive.jpg", "fields.jps", "pair.mpo", "big.mpo", "tail.jps", "tail.jpg"};
    for(size_t i = 0; i < 7; ++i) {
        const std::string aPath = std::string(aDirectory) + "/" + aNames[i];
        const std::vector<stUByte_t> anOriginal = readFile(std::string(CORRECTION_DATA) + "/" + aNames[i]);
        const std::vector<stUByte_t> anExpected = readFile(std::string(CORRECTION_DATA) + "/" + aNames[i] + ".expected");
        writeFile(aPath, anOriginal);
        chmod(aPath.c_str(), 0640);
        const char aNote[] = "keep this attribute";
        check(setxattr(aPath.c_str(), "user.sview-test", aNote, sizeof(aNote), 0) == 0, "attribute fixture");
        const StFormat aFormat = (i == 3 || i == 4) ? StFormat_SeparateFrames : StFormat_SideBySide_RL;
        const bool toSwap = i == 3 || i == 4;
        {
            StFileTransaction aTransaction(StString(aPath.c_str()));
            std::vector<stUByte_t> aCandidate, aRead;
            check(aTransaction.isValid(), "temporary copy");
            const int aPrepared = StPictureCorrection::prepare(aTransaction.original(), aFormat, toSwap, aCandidate);
            check(aPrepared == STEREO_JPEG_OK, "shared codec");
            if(aPrepared != STEREO_JPEG_OK) { unlink(aPath.c_str()); continue; }
            check(aCandidate == anExpected, "independent full-byte oracle");
            check(aTransaction.write(aCandidate) && aTransaction.readBack(aRead) && aRead == aCandidate, "disk byte read-back");
            check(StPictureCorrection::verify(aTransaction.original(), aRead, aFormat, toSwap), "chosen decoded eyes read back");
            check(aTransaction.commit(aRead), "verified replacement");
        }
        struct stat aStat;
        char anAttribute[64] = {};
        check(readFile(aPath) == anExpected, aNames[i]);
        check(stat(aPath.c_str(), &aStat) == 0 && (aStat.st_mode & 07777) == 0640, "mode retained");
        check(getxattr(aPath.c_str(), "user.sview-test", anAttribute, sizeof(anAttribute)) == sizeof(aNote)
           && !std::memcmp(anAttribute, aNote, sizeof(aNote)), "attribute retained");
        writeFile(aPath, anOriginal);
        {
            StFileTransaction aTransaction(StString(aPath.c_str()));
            std::vector<stUByte_t> aCandidate, aRead;
            StPictureCorrection::prepare(anOriginal, aFormat, toSwap, aCandidate);
            stereo_jpeg_info aMetadata;
            if(stereo_jpeg_inspect(aCandidate.data(), aCandidate.size(), &aMetadata)) { return 2; }
            aCandidate[aMetadata.image_size[0] - 1] = 0;
            check(aTransaction.write(aCandidate) && aTransaction.readBack(aRead)
               && !StPictureCorrection::verify(anOriginal, aRead, aFormat, toSwap), "corrupt temporary refused");
        }
        check(readFile(aPath) == anOriginal, "failed verification keeps original");
        {
            StFileTransaction aTransaction(StString(aPath.c_str()));
            std::vector<stUByte_t> aCandidate;
            StPictureCorrection::prepare(anOriginal, aFormat, toSwap, aCandidate);
            check(aTransaction.write(aCandidate), "race fixture prepared");
            std::vector<stUByte_t> anExternal = anOriginal;
            anExternal.push_back(0x5a);
            writeFile(aPath, anExternal);
            check(!aTransaction.commit(aCandidate) && readFile(aPath) == anExternal, "concurrent original change preserved");
        }
        const std::string aLink = std::string(aDirectory) + "/link";
        check(symlink(aNames[i], aLink.c_str()) == 0, "symlink fixture");
        { StFileTransaction aTransaction(StString(aLink.c_str())); check(!aTransaction.isValid(), "symlink refused"); }
        unlink(aLink.c_str());
        unlink(aPath.c_str());
    }
    check(rmdir(aDirectory) == 0, "no temporary files left");
    return THE_FAILURES ? 1 : 0;
}
