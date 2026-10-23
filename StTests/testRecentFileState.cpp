// MIT license; see docs/license-mit.txt.
#include <StGL/StPlayList.h>
#include <StFile/StFileNode.h>
#include <StThreads/StProcess.h>
#include <cstdio>

int main() {
    StArgumentsMap aSaved;
    aSaved.add(StArgument("left0", "/synthetic/left.png"));
    aSaved.add(StArgument("right0", "/synthetic/right.png"));
    aSaved.add(StArgument("packing0", "checkerboardRL"));
    aSaved.add(StArgument("swap0", "1"));
    aSaved.add(StArgument("time0", "725"));
    StPlayList aList(0);
    aList.loadRecentList(aSaved.toString());
    aList.openRecent(0);
    std::shared_ptr<StFileNode> aFile;
    std::shared_ptr<StStereoParams> aParams;
    if(!aList.getCurrentFile(aFile, aParams)) { return 1; }
    aList.currentToRecent();
    StArgumentsMap aRoundTrip;
    aRoundTrip.parseString(aList.dumpRecentList());
    bool ok = aRoundTrip["packing0"].getValue() == "checkerboardRL"
           && aRoundTrip["swap0"].getValue() == "1"
           && aRoundTrip["time0"].getValue() == "725";
    std::printf("reopen packing + swap + resume: %s\n", ok ? "PASS" : "FAIL");
    aList.clear();
    aList.addOneFile(StString("/synthetic/other-left.png"), StString("/synthetic/other-right.png"));
    aList.getCurrentFile(aFile, aParams);
    aList.currentToRecent();
    StArgumentsMap aNext;
    aNext.parseString(aList.dumpRecentList());
    bool fresh = aNext["packing0"].getValue() == "auto"
              && aNext["swap0"].getValue() == "0";
    std::printf("next file does not inherit override: %s\n", fresh ? "PASS" : "FAIL");
    return ok && fresh ? 0 : 1;
}
