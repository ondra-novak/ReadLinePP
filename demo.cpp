
#include <iostream>

#include "readlinepp.h"

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
    });
    rl.setAppName("rldemo");
    std::string line;
    while (rl.read(line)) {
        std::cout << line << std::endl;
    }


    return 0;

}
