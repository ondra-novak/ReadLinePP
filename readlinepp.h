#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <cstring>
#include <regex>
#include <mutex>


struct ReadLineConfig {
    ///Prompt (can be changed later)
    std::string prompt;
    ///Limit of history (0 = unlimited)
    unsigned int history_limit = 0;
    ///word break characters for completion generator
    std::string word_break_chars = " \t\n\"\\'`@$><=;|&{(";
};

///ReadLine C++ wrapper around libreadline
/**
 * Note it wraps only basic functions. However
 * you can extend this class to support more functions. See further
 * documentation for the functions run_locked(), saveRLState() and restoreRLState()
 *
 * Because readline doesn't support multiple instances, the implementation
 * handles switching underlying context if the object need to interact
 * with the readline library while there is other instance which was interacting before.
 * One instance is always attached to readline
 * interface whether others are not. Any interaction with other instance
 * causes detach of currently attached instance and attaching the instance
 * which is requiring interaction.
 *
 * (if only one instance exists, the attach is performed only once)
 *
 * The object itself is not MT safe - it only protects readline API by global lock, but
 * internals of the class is not MT protected. Use synchronization if you need MT saftey
 *
 * The class supports customization of completion and store and restore the
 * history (each instance has own history and own completion rules)
 *
 * @note Because there is a global lock, you can experience a deadlock or
 * long lasting lock if one instance is waiting on read() and an other
 * instance is accessed and this instance also need to run something
 * under that lock (including destructors). Such operation will be blocked
 * until the read operation is finished.
 *
 *
 */
class ReadLine {
public:

    ///Proposal callback
    /** This function is called by completion function to register a new proposal
     * @param s proposal
     */
    using ProposalCallback = std::function<void(const std::string &s)>;

    ///Function of proposal generator
    /**
     * @param word C pointer to a word used as base for suggestions (match)
     * @param word_size size of the word (don't rely on terminating zero)
     * @param m regexp matches from pattern matching
     * @param cb callback function called for every proposal
     */
    using GenFn = std::function<void(const char *word, std::size_t word_size ,const std::cmatch &m, const ProposalCallback &cb)>;

    struct CLikeDeleter {
        void operator()(char *str) const;
    };

    using ProposalItem = std::unique_ptr<char, CLikeDeleter>;

    using ProposalList = std::vector<ProposalItem>;



    ///Extends GenFn with ability to create function from list of specified items
    class ProposalGenerator: public GenFn {
    public:

        template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<const char *>(), std::declval<std::size_t>(), std::declval<const std::cmatch &>(), std::declval<const ProposalCallback &>()))>
        ProposalGenerator(Fn &&fn):GenFn(std::forward<Fn>(fn)) {}
        ProposalGenerator(const std::initializer_list<std::string> &options);
//        ProposalGenerator(const ProposalGenerator &other):GenFn(other) {}
        ProposalGenerator(const GenFn &other):GenFn(other) {}
        ProposalGenerator(GenFn &&other):GenFn(std::move(other)) {}

    };

    ///This generator generates file suggestions
    /**
     * Path where to search
     * @param rootPath root path where to search
     * @param pattern pattern (regex) - empty pattern means that no filtering is done.
     * @param pathname allow pathnames (path+name) which means, that it is possible to refer different directory
     * @return generator object
     */
    static ProposalGenerator fileLookup(const std::string &rootPath, const std::string &pattern=std::string(), bool pathname = true);

    ///Pattern - uses regex, but we need to not have constructor explicit
    class Pattern: public std::regex {
    public:
        using std::regex::regex;
        Pattern(const char *x):std::regex(x) {}
    };

    ///{"pattern",generator}
    struct CompletionItem {
        Pattern pattern;
        ProposalGenerator generator;
    };

    ///Completion list
    using CompletionList = std::vector<CompletionItem>;


    ///Construct ReadLine object
    ReadLine();
    ///Construct ReadLine object
    explicit ReadLine(const ReadLineConfig &cfg);
    ///You can move the instance (detach)
    /**
     * @param other source instance
     *
     * @note performs detach during operation
     */
    ReadLine(ReadLine &&other);

    ///You can't copy the instance
    ReadLine(const ReadLine &other) = delete;
    ///You can assign by moving
    /**
     * @param other source instance
     *
     * @note performs detach during operation
     */
    ReadLine &operator=(ReadLine &&other);
    ///You cannot copy
    ReadLine &operator=(const ReadLine &other) = delete;

    ///Destructor
    virtual ~ReadLine();

    ///Read line (global lock)
    /**
     * @param line reference to variable which receives the line
     * @retval true line read
     * @retval false EOF (control+D) has been detected
     *
     * @note function holds global lock during its execution!
     */
    bool read(std::string &line);

    ///Sets prompt
    void setPrompt(const std::string &prompt);
    ///Sets prompt
    void setPrompt(std::string &&prompt);


    ///Change config (detach)
    /**
     * Changes configuration.
     * @param config new configuration
     *
     * Causes detach()
     */
    void setConfig(const ReadLineConfig &config);

    ///Retrieve config
    const ReadLineConfig &getConfig() const;

    ///Sets completion list - used by default implementation of onComplete;
    /**
     * Completion list can be defined as array of rules
     * @code
     * {
     *   {"pattern",generator},
     *   {"pattern",{"word1","word2","word3"...}}
     * }
     * @encode
     *
     * The pattern is always regexp pattern (see regex_match). It can also contains submatches,
     * that are passed to the generator. During completion, begin of the
     * entered line is tested against patterns and if the pattern matches,
     * the generator is called.
     *
     * @note if the pattern is "", it matches only for the very first word
     *
     * @param list
     */
    void setCompletionList(CompletionList &&list);

    ///Sets app name
    /**
     * Function just generates path to history file as ~/.appName_history
     * @param appName name of application
     *
     * It also tries to load that history file to the memory
     *
     * @see setHistoryFile
     */
    void setAppName(const std::string &appName);

    ///Sets history file
    /**
     * @param file history file
     *
     * Loads history to the memory (if exists). It also stores history
     * during destruction (stores only newly added items)
     *
     * @note To avoid locking, the function postpones loading of
     * history at time of the first attach
     *
     */
    void setHistoryFile(const std::string &file);


    ///Gets name of history file
    const std::string &getHistoryFile() const;

    ///Retrieve history (detach)
    /**
     * @return vector of all strings in history ordered from least recent to most recent
     *
     * @note internally detaches from readline interface
     */
    std::vector<std::string> getHistory() const;

    ///Clears history (detach)
    /**
     * Doesn't use clear_history() of readline, it perform own cleaning,
     * so it doesn't perform restoreRLState()
     *
     * @note internally detaches from readline interface
     */
    void clearHistory();

public: //overwrites

    ///Auto completion
    /**
     * @param wholeLine contains whole line as standar C string
     * @param start offset of start of character sequence
     * @param end offset of end of character sequence
     * @param cb callback function which receives all suggestions (as std::string)
     * @retval true completion handled
     * @retval false completion was not handled
     *
     * @note Default implementation performs completions through completion list.
     * If you overwrite this function, you should call base implementation to
     * keep this feature available
     */
    virtual bool onComplete(const char *wholeLine, std::size_t start, std::size_t end, const ProposalCallback &cb);


    ///Allows to filter items which go to history
    /**
     * @param line entered line
     * @retval true store line to history
     * @retval false don't store this line to history
     *
     * @note default implementation stores everything to the history
     */
    virtual bool filterHistory(const std::string &line);

    ///Postprocess the final line
    /** Allows to modify line returned to the caller */
    /**
     * @param line entered line, you can freely modify content
     */
    virtual void postprocess(std::string &line);

    ///Allows to edit proposal list
    /**
     * @param wholeLine whole entered line
     * @param start start of word
     * @param end end of word
     * @param list current proposal list.
     *
     * Proposal list is list of pointer to C-like strings (terminated by zero);
     * If you want to replace a string, you need to use allocProposalItem to convert string
     * to such item
     *
     * @note default implementation is empty
     */
    virtual void editProposals(const char *wholeLine, std::size_t start, std::size_t end, ProposalList &list);


    ///Allows to define list of word breaking characters before completion funtion is started
    /**
     * @param line whole line
     * @param size size of line in characters
     * @param pos current cursor position
     * @return pointer to C-like null terminated string containing all characters which
     * breaks words. You must keep pointer valid during whole lifetime of the instance
     * (or until you supply new pointer).
     *
     * @note default implementation returns ReadLineConfig::word_break_chars
     */
    virtual const char *completionWordBreakHook(const char *line, std::size_t size, std::size_t pos);



protected:

    ReadLineConfig _config;
    std::string _history_file;
    int _appended = 0;
    CompletionList _completionList;

    mutable std::atomic<bool> _dirty;
    mutable struct _hist_state * _state = nullptr;
    mutable bool _need_load_history = false;
    std::string _prev_line;

    ///Save readline state
    /**
     * Transfers readline's global state to object's variables because
     * the global state is being overwritten. This allows to have multiple
     * instances of the ReadLine object above single global instance of
     * c-library
     *
     * You need to overwrite this function, if you class uses global
     * state which is not covered by this class (history, history limit,
     * completion)
     *
     * This function is called from run_locked() or detach()
     *
     * The function must be called under run_locked()
     */
    virtual void saveRLState() const;
    ///Restores readline state
    /**
     * Calls readline functions and resets global variables by variables
     * of this object. This function is called when current object
     * need access readline interace.
     *
     * Function is called from run_locked when current context is not active.
     *
     * Function must be called under run_locked().
     */
    virtual void restoreRLState() const;



    ///Detaches instance from readline interface
    /**
     * You need to detach this instance if you need to explore the state
     * Function calls restoreRLState() if is it necessary.
     *
     * If the instance is not attached, function does nothing (it also
     * skips acquiring the lock)
     */

    void detach() const;

    ///Executes operation in locked state
    /**
     * Function ensures, that active instance is this instance, performs
     * syncStateOut() if it is necessary.
     * @param fn Function called in locked state. Any interaction with
     * readline interface must be performed under locked state (there
     * are some exceptions for functions that doesn't interacts with
     * its global state)
     */
    template<typename Fn>
    void run_locked(Fn &&fn) {
        std::lock_guard<std::recursive_mutex> _(gmx);
        if (curInst != this) {
            if (curInst) curInst->saveRLState();
            curInst = this;
            curInst->_dirty = true;
            restoreRLState();
        }
        fn();
    }

    ///Allocate proposal item
    /**
     * Need to call this function to edit proposals
     * @param str string
     * @return ProposalItem
     *
     * @see editProposals
     */
    static ProposalItem allocProposalItem(const std::string &str);
    ///Allocate proposal item
    /**
     * Need to call this function to edit proposals
     * @param str string
     * @param offset offset of substring
     * @param len length of substring
     * @return ProposalItem
     *
     * @see editProposals
     */
    static ProposalItem allocProposalItem(const std::string &str, std::size_t offset, std::size_t len);
    ///global mutex
    static std::recursive_mutex gmx;
    ///current instance
    static ReadLine *curInst;
    ///completion global function
    static char **global_completion (const char *, int start, int end);
    ///completion work break hook implementation
    static char *completion_word_break_hook();
    ///initializes libraries
    static void initLibs();
private:
    ///internal initialize
    static void initLibsInternal();



};

