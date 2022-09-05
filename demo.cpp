
#include <fstream>
#include <iostream>

#include "readlinepp.h"

ReadLine::GenFn extractFile(std::string path) {
    return [path](const char *word, std::size_t word_len, const std::cmatch &m, const ReadLine::ProposalCallback &cb) {
        std::string fullpath = path+"/"+std::string(m[1].first,m[1].second);
        std::ifstream x(fullpath);
        if (!x) return;
        std::string ln;
        while (!!std::getline(x,ln)) {
            auto n = ln.find_first_not_of(" \t");
            if (n != ln.npos) {
                ln.erase(0,n);
                if (ln.compare(0,word_len,word) == 0) {
                    cb(ln);
                }
            }
        }
    };
}


int main(int argv, char **argc) {

    ReadLine rl;

    std::cout << "ReadLine++ demo. Try press TAB twice. To exit press Ctrl+D" << std::endl;
    rl.setPrompt(">");
    rl.setCompletionList({
        {"",{"hello","hi","file","csource"}},
        {"hello ",{"world!","universe!","people!"}},
        {"hi ",{"ondra","franta"}},
        {"file ",ReadLine::fileLookup(".")},
        {"csource ",ReadLine::fileLookup(".",".*\\.c|.*\\.cpp|.*\\.h|.*\\/")},
        {"csource ([^ ]+) ",extractFile(".")},
    });
    rl.setAppName("rldemo");
    std::string line;
    while (rl.read(line)) {
        std::cout << line << std::endl;
    }


    return 0;

}
