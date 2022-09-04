#include "readlinepp.h"

#include <readline/readline.h>
#include <readline/history.h>
#include <mutex>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/stat.h>


std::recursive_mutex ReadLine::gmx;
ReadLine *ReadLine::curInst = nullptr;

//THIS UGLY C-HYBRID FUNCTION IS BRIDGE BETWEEN UGLY C INTERFACE AND C++ INTERFACE
//function is super global
char **ReadLine::global_completion (const char *, int start, int end) {

    //all is static/global as the readline itself is not MT safe and
    //this function cannot be invoked in parallel

    static ProposalList _compl_tmp;
    static ProposalCallback _compl_cb = [](const std::string &sug){
        _compl_tmp.push_back(allocProposalItem(sug));
    };

    if (curInst) {
        _compl_tmp.clear();
        if (curInst->onComplete(rl_line_buffer, start, end, _compl_cb)) {
            char **list;
            //this is over
            rl_attempted_completion_over = 1;
            //this function contains bunch of C patterns - UNSAFE CODE!
            //we will use malloc and raw string pointers
            //there is no way how to overcome this
            //because readline library has C interface

            //if generated list of completions is empty
            //we must return nullptr - we cannot return empty list
            //because it sigfaults
            if (_compl_tmp.empty()) {
                return nullptr;

            //special case is when list of completions contains one item
            //we generate list of two items
            //where first item is our string
            //second item is NULL
            }else if (_compl_tmp.size() == 1) {
                list = reinterpret_cast<char **>(calloc(2,sizeof(char *)));
                list[0] = _compl_tmp[0].release();
                list[1] = nullptr;

            //if multiple matches
            //we must return list + 2 extra items
            //first item contains common part of all matches
            //other items are initialized from completion list
            //and last item is NULL
            } else {
                list = reinterpret_cast<char **>(calloc(_compl_tmp.size()+2,sizeof(char *)));

                //we must compute common part of all matches
                std::size_t common = std::numeric_limits<std::size_t>::max();
                for (const auto &x: _compl_tmp) {
                    char *z1 = x.get();
                    char *z2 = _compl_tmp[0].get();
                    std::size_t l = 0;
                    while (l < common && z1[l] && z1[l] == z2[l] ) ++l;
                    common = l;
                    if (common == 0) break;
                }
                char *comstr = static_cast<char *>(malloc(common+1));
                strncpy(comstr, _compl_tmp[0].get(), common);
                comstr[common] = 0;
                list[0] = comstr;

                //copy items, starting at index 1
                char **it = list+1;
                //it is iterator
                for (auto &x: _compl_tmp) {
                    //assign to c-array
                    *it++ = x.release();
                }
                //set null as final item
                *it++ = nullptr;

            }
            //return list (rl will handle deallocation)
            return list;
        } else {
            return NULL;
        }
    } else {
        return NULL;
    }
}

char *ReadLine::completion_word_break_hook() {
    if (curInst) {
        return const_cast<char *>(curInst->completionWordBreakHook(rl_line_buffer, rl_end, rl_point));
    } else {
        return rl_completer_word_break_characters;
    }
}

void ReadLine::CLikeDeleter::operator()(char *str) const {
    if (str) rl_free(str);
}

bool ReadLine::filterHistory(const std::string &line) {
    bool ok = !line.empty() && line != _prev_line;
    if (ok) _prev_line = line;
    return ok;
}

void ReadLine::postprocess(std::string &line) {
    //empty - no postprocessing
}

const char* ReadLine::completionWordBreakHook(const char *,std::size_t , std::size_t ) {
    return _config.word_break_chars.c_str();
}

ReadLine::ProposalItem ReadLine::allocProposalItem(const std::string &str) {
    return ProposalItem(strdup(str.c_str()));
}

void ReadLine::editProposals(const char *wholeLine, std::size_t start, std::size_t end, ProposalList &list) {
    //empty;
}

ReadLine::ProposalItem ReadLine::allocProposalItem(const std::string &str, std::size_t offset, std::size_t len) {
    offset = std::min(offset, str.length());
    len = std::min(len,str.length()-offset);
    char *c = reinterpret_cast<char *>(malloc(len+1));
    std::copy(str.begin()+offset, str.begin()+offset+len, c);
    c[len] = 0;
    return ProposalItem(c);

}

void ReadLine::initLibsInternal() {
    rl_initialize ();
    using_history();
    rl_attempted_completion_function = &global_completion;
    rl_completion_word_break_hook = &completion_word_break_hook;

}

static std::once_flag initLibsFlag;


void ReadLine::initLibs() {
    std::call_once(initLibsFlag, initLibsInternal);
}




void ReadLine::restoreRLState() const {
    if (_config.history_limit) {
        stifle_history(_config.history_limit);
    } else{
        unstifle_history();
    }
    if (_state) {
        history_set_history_state(_state);
    } else {
        HISTORY_STATE st = {};
        history_set_history_state(&st);
    }
    if (_need_load_history) {
        read_history(_history_file.c_str());
        _need_load_history = false;
    }
}


ReadLine::~ReadLine() {
    if (!_history_file.empty() || !_need_load_history) {
        run_locked([&]{
            if (append_history(_appended, _history_file.c_str())) {
                write_history(_history_file.c_str());
            }
            if (_config.history_limit) history_truncate_file(_history_file.c_str(), _config.history_limit);
        });
    }
    detach();
    clearHistory();
}

bool ReadLine::read(std::string &line) {
    bool ok;
    run_locked([&]{
       auto ln = readline(_config.prompt.c_str());
       if (!ln) {
           ok = false;
       } else {
           line = ln;
           if (filterHistory(line)) {
               add_history(ln);
               ++_appended;
           }
           free(ln);
           ok = true;
       }
    });
    if (ok) postprocess(line);
    return ok;
}

ReadLine::ReadLine():_dirty(false) {
    initLibs();
}

ReadLine::ReadLine(const ReadLineConfig &cfg):_config(cfg),_dirty(false) {
    initLibs();
}

ReadLine::ReadLine(ReadLine &&other)
:_config(std::move(other._config))
,_history_file(std::move(other._history_file))
,_appended(other._appended)
,_completionList(std::move(other._completionList))
,_need_load_history(std::move(other._need_load_history))
{
    other.detach();
    _state = other._state;
}

ReadLine& ReadLine::operator =(ReadLine &&other) {
    if (this != &other) {
        other.detach();
        _config = std::move(other._config);
        _history_file = std::move(other._history_file);
        _appended = other._appended;
        _completionList = std::move(other._completionList);
        _need_load_history = other._need_load_history;
        clearHistory();
        _state = other._state;
        other._state = nullptr;
    }
    return *this;
}

void ReadLine::setPrompt(const std::string &prompt) {
    _config.prompt =prompt;
}

void ReadLine::setPrompt(std::string &&prompt) {
    _config.prompt = std::move(prompt);
}

void ReadLine::saveRLState() const {
    if (_state) free(_state);
    _state = history_get_history_state();
    _dirty = false;
}

bool ReadLine::onComplete(const char *wholeLine, std::size_t start, std::size_t end, const ProposalCallback &cb) {
    if (_completionList.empty()) return false;

    const char *word = wholeLine + start;
    auto sz = end - start;
    std::cmatch m;
    for (const auto &x: _completionList) {
        if (std::regex_match<const char *>(wholeLine, wholeLine+start, m, x.pattern)) {
            x.generator(word, sz, m, cb);
        }
    }

    return true;
}

void ReadLine::setCompletionList(CompletionList &&list) {
    _completionList = std::move(list);
}

void ReadLine::setConfig(const ReadLineConfig &config) {
    detach();
    _config = config;
}

const ReadLineConfig &ReadLine::getConfig() const {
    return _config;
}


ReadLine::ProposalGenerator::ProposalGenerator(const std::initializer_list<std::string> &options)
:GenFn([lst = std::vector<std::string>(options)](const char *word, std::size_t sz,const std::cmatch &m, const ProposalCallback &cb) {
    for (const auto &x: lst) {
        if (x.compare(0, sz, word) == 0) cb(x);
    }
})
{
}

void ReadLine::setAppName(const std::string &appName) {
    const char *homedir = getenv("HOME");
    if (homedir) {
        struct passwd *pw = getpwuid(getuid());
        homedir = pw->pw_dir;
    }
    std::string path = homedir;
    path.append("/.").append(appName).append("_history");
    setHistoryFile(path);
}

void ReadLine::setHistoryFile(const std::string &file) {
    _history_file = file;
    _need_load_history = true;
}


const std::string& ReadLine::getHistoryFile() const {
    return _history_file;
}


void ReadLine::clearHistory() {
    detach();
    if (_state) {
        for (int i = 0; i < _state->length; ++i) {
            free_history_entry(_state->entries[i]);
        }
        rl_free(_state->entries);
        rl_free(_state);
    }
}


void ReadLine::detach() const {
    if (_dirty == true) {
        std::lock_guard<std::recursive_mutex> _(gmx);
        if (curInst == this) {
            saveRLState();
            curInst = nullptr;
        }
    }
}

std::vector<std::string> ReadLine::getHistory() const {
    std::vector<std::string> out;
    detach();
    if (_state) {
        for (int i = 0; i < _state->length; ++i) {
            out.push_back(_state->entries[i]->line);
        }
    }
    return out;
}

class FileLookup {
public:
    FileLookup(const std::string &rootPath, const std::string &pattern, bool pathname)
        :_root(rootPath)
        ,_pattern(pattern)
        ,_match_all(pattern.empty())
        ,_pathname(pathname) {}

    void operator()(const char *word, std::size_t word_size ,const std::cmatch &m, const ReadLine::ProposalCallback &cb) const;

protected:
    std::string _root;
    std::regex _pattern;
    bool _match_all;
    bool _pathname;
};

ReadLine::ProposalGenerator ReadLine::fileLookup(const std::string &rootPath,
        const std::string &pattern, bool pathname) {
    return ProposalGenerator(GenFn(FileLookup(rootPath, pattern, pathname)));
}

inline void FileLookup::operator ()(const char *word, std::size_t word_size,
        const std::cmatch &m, const ReadLine::ProposalCallback &cb) const {
    std::string r = _root;
    std::string w (word, word_size);
    std::string entry;
    std::size_t count = 0;
    if (_pathname && w.find('/') != w.npos) {
        //word starting by / - means that we search from root
        if (w[0] == '/') {
            r = "/";    //set root
            entry = r;
            w.erase(0,1);  //erase separator
        //if there is a path and it doesn't end by '/' append it now
        } else if (!r.empty() || r.back() != '/')  {
            r.push_back('/');
        }
        auto sep = w.rfind('/'); //find last /
        if (sep != w.npos) {
            r.append(w.begin(), w.begin()+sep+1); //append path to r
            entry.append(w.begin(), w.begin()+sep+1); //append path to entry
            w.erase(w.begin(), w.begin()+sep+1); //remove from w
        }
    }
    auto rs = r.length();
    auto es = entry.length();

    std::string only_entry;
    //why we use opendir instead filesystem
    //we have already dependency on Posix, where opendir is part of it
    //and we don't include additional dependency on filesystem/boost filesystem
    //which is also problematic on edge between C++14 and C++17
    //so lets stick with old fashion "Posix way"
    DIR *dir = opendir(r.c_str());
    if (dir) {
        const struct dirent *e = readdir(dir);
        while (e) {
            const char *dname_beg = e->d_name;
            const char *dname_end = e->d_name+std::strlen(e->d_name);
            entry.resize(es);
            entry.append(dname_beg, dname_end);
            bool isdir = false;
            switch (e->d_type) {
                default: isdir = false;break;
                case DT_DIR: isdir = true;break;
                case DT_LNK:
                case DT_UNKNOWN: if (_pathname) {
                        struct stat st;
                        r.resize(rs);
                        r.push_back('/');
                        r.append(dname_beg, dname_end);
                        stat(r.c_str(), &st);
                        isdir = S_ISDIR(st.st_mode);
                } else {
                    isdir = false;
                }
            }
            if (_pathname && isdir) {
                entry.push_back('/');
            }
            if (_match_all || std::regex_match(entry,_pattern)) {
                if (entry.compare(0,word_size,word) ==0) {
                    count++;
                    if (count == 1 && isdir) only_entry = entry;
                    cb(entry);
                }
            }
            e = readdir(dir);
        }
        closedir(dir);
    }
    if (count == 1 && !only_entry.empty()) {
        this->operator ()(only_entry.c_str(), only_entry.length(), m, cb);
    }
}
