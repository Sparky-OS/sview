// The messages language from the environment, as gettext reads it: LC_ALL, then LC_MESSAGES, then LANG name the
// locale, LANGUAGE lists the preferred languages for any locale but C; Chinese keeps its territory.
// MIT license; see docs/license-mit.txt.
#include <StThreads/StResourceManager.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

struct Case { const char* All; const char* Messages; const char* Lang; const char* Language; const char* Expected; };

int main() {
    static const Case THE_CASES[] = {
        { NULL,          NULL,          "pt_BR.UTF-8", NULL,       "pt" },
        { NULL,          NULL,          "pt_PT.UTF-8", NULL,       "pt" },
        { NULL,          NULL,          "pl_PL.UTF-8", NULL,       "pl" },
        { NULL,          NULL,          "fr_CA.UTF-8", NULL,       "fr" },
        { NULL,          NULL,          "de_DE.UTF-8", NULL,       "de" },
        { NULL,          NULL,          "es_MX.UTF-8", NULL,       "es" },
        { NULL,          NULL,          "ja_JP.UTF-8", NULL,       "ja" },
        { NULL,          NULL,          "ko_KR.UTF-8", NULL,       "ko" },
        { NULL,          NULL,          "zh_CN.UTF-8", NULL,       "zh" },
        { NULL,          NULL,          "zh_SG",       NULL,       "zh" },
        { NULL,          NULL,          "zh_TW.UTF-8", NULL,       "zh-tw" },
        { NULL,          NULL,          "zh_HK.UTF-8", NULL,       "zh-tw" },
        { NULL,          NULL,          "sr_RS@latin", NULL,       "sr" },
        { NULL,          NULL,          "en_US.UTF-8", NULL,       "en" },
        { NULL,          NULL,          "C.UTF-8",     "pt_BR",    "default" },
        { "C",           NULL,          "pt_BR.UTF-8", NULL,       "default" },
        { "POSIX",       NULL,          "pt_BR.UTF-8", NULL,       "default" },
        { NULL,          NULL,          NULL,          NULL,       "default" },
        { "de_DE.UTF-8", "pl_PL.UTF-8", "pt_BR.UTF-8", NULL,       "de" },
        { NULL,          "pl_PL.UTF-8", "en_US.UTF-8", NULL,       "pl" },
        { NULL,          NULL,          "de_DE.UTF-8", "ja:en",    "ja" },
        { NULL,          NULL,          "en_US.UTF-8", "zh_TW:zh", "zh-tw" },
        { NULL,          NULL,          "en_US.UTF-8", "",         "en" },
    };
    int aFailures = 0;
    for(size_t anIter = 0; anIter < sizeof(THE_CASES) / sizeof(THE_CASES[0]); ++anIter) {
        const Case& aCase = THE_CASES[anIter];
        const char* aNames[4]  = { "LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE" };
        const char* aValues[4] = { aCase.All, aCase.Messages, aCase.Lang, aCase.Language };
        for(int aVar = 0; aVar < 4; ++aVar) {
            if(aValues[aVar] != NULL) {
                setenv(aNames[aVar], aValues[aVar], 1);
            } else {
                unsetenv(aNames[aVar]);
            }
        }
        const StString aLang = StResourceManager::languageFromEnvironment(StString("default"));
        if(std::strcmp(aLang.toCString(), aCase.Expected) != 0) {
            std::printf("FAIL case %d: %s, expected %s\n", int(anIter), aLang.toCString(), aCase.Expected);
            ++aFailures;
        }
    }
    std::printf("%d failing cases\n", aFailures);
    return aFailures == 0 ? 0 : 1;
}
