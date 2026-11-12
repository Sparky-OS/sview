// Every save reason is present in all required catalogues, with intact placeholders.
// MIT license; see docs/license-mit.txt.
#include <fstream>
#include <string>
#include <map>
#include <set>
#include <regex>
#include <cstdio>

static std::map<int,std::string> read(const std::string& thePath) {
    std::ifstream f(thePath.c_str());
    std::map<int,std::string> result;
    std::string line;
    while(std::getline(f,line)) {
        size_t pos=line.find('=');
        if(pos == std::string::npos || line.empty() || line[0] < '0' || line[0] > '9') { continue; }
        result[std::stoi(line.substr(0,pos))]=line.substr(pos+1);
    }
    return result;
}
static std::multiset<std::string> placeholders(const std::string& theText) {
    std::multiset<std::string> result;
    std::regex expression("\\{[0-9]+\\}");
    for(std::sregex_iterator i(theText.begin(),theText.end(),expression), end; i != end; ++i) { result.insert(i->str()); }
    return result;
}
int main() {
    const char* languages[]={"english","polish","portuguesebr","french","german","spanish","japanese","korean","chinese","chineset"};
    const char* programs[]={"StImageViewer","StMoviePlayer"};
    int failures=0;
    for(int p=0;p<2;++p) {
        std::string base=std::string(SVIEW_SOURCE)+"/"+programs[p]+"/lang/";
        const auto english=read(base+"english/"+programs[p]+".lng");
        for(const char* lang:languages) {
            const auto text=read(base+lang+"/"+programs[p]+".lng");
            for(int id=2100;id <= (p == 0 ? 2119 : 2114);++id) {
                auto entry=text.find(id), expected=english.find(id);
                bool ok=entry != text.end() && expected != english.end() && !entry->second.empty()
                     && placeholders(entry->second) == placeholders(expected->second);
                if(!ok) { std::printf("FAIL %s %s id=%d\n",programs[p],lang,id); ++failures; }
            }
        }
    }
    std::printf("save languages: %d failures\n",failures);
    return failures ? 1 : 0;
}
