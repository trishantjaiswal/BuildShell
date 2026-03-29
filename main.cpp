#include <iostream>
#include <limits.h>
#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <string>
#include <errno.h>
#include <vector>
#include <set>
#include <cstring>
#include <sys/wait.h>
#include <filesystem>
#include <fcntl.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>

// Set of built-in shell commands
std::set<std::string> builtin_command = {
    "exit", "echo", "type", "pwd", "cd", "history"
};

// Stores redirection flags and target files
struct redirection {
    bool redirect_stdout = false;
    bool redirect_stderr = false;
    bool append_stdout   = false;
    bool append_stderr   = false;
    std::string stdout_file;
    std::string stderr_file;
};

// Used to restore original stdout/stderr after redirection
struct SavedFds {
    int out = -1;
    int err = -1;
};

static int last_written_history = 0;

// Parses redirection operators (>, >>, 2>, etc.) and removes them from args
redirection parse_redirection(std::vector<std::string>& args) {
    redirection r;
    for (size_t i = 0; i < args.size();) {
        if (args[i] == ">" || args[i] == "1>") {
            r.redirect_stdout = true;
            r.append_stdout   = false;
            r.stdout_file     = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == ">>" || args[i] == "1>>") {
            r.redirect_stdout = true;
            r.append_stdout   = true;
            r.stdout_file     = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "2>") {
            r.redirect_stderr = true;
            r.append_stderr   = false;
            r.stderr_file     = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else if (args[i] == "2>>") {
            r.redirect_stderr = true;
            r.append_stderr   = true;
            r.stderr_file     = args[i + 1];
            args.erase(args.begin() + i, args.begin() + i + 2);
        } else {
            i++;
        }
    }
    return r;
}

// Applies redirection using dup2 and returns original fds for restoration
SavedFds apply_redirections(const redirection& r) {
    SavedFds fds;

    if (r.redirect_stdout) {
        fds.out = dup(STDOUT_FILENO);
        int flags = O_WRONLY | O_CREAT | (r.append_stdout ? O_APPEND : O_TRUNC);
        int fd = open(r.stdout_file.c_str(), flags, 0644);
        if (fd < 0) { perror("open stdout"); exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    if (r.redirect_stderr) {
        fds.err = dup(STDERR_FILENO);
        int flags = O_WRONLY | O_CREAT | (r.append_stderr ? O_APPEND : O_TRUNC);
        int fd = open(r.stderr_file.c_str(), flags, 0644);
        if (fd < 0) { perror("open stderr"); exit(1); }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    return fds;
}

// Restores stdout/stderr after executing builtin commands
void restore_builtin_redirection(const SavedFds& fds) {
    if (fds.out != -1) { dup2(fds.out, STDOUT_FILENO); close(fds.out); }
    if (fds.err != -1) { dup2(fds.err, STDERR_FILENO); close(fds.err); }
}

// Parses command string into arguments, handling quotes and escapes
std::vector<std::string> parse_command(const std::string& command) {
    std::vector<std::string> args;
    std::string current;

    enum State { NORMAL, SINGLE, DOUBLE };
    State state = NORMAL;

    for (size_t i = 0; i < command.size(); i++) {
        char c = command[i];

        if (state == NORMAL) {
            if (isspace(c)) {
                if (!current.empty()) { args.push_back(current); current.clear(); }
            } else if (c == '\'') {
                state = SINGLE;
            } else if (c == '"') {
                state = DOUBLE;
            } else if (c == '\\') {
                if (i + 1 < command.size()) current += command[++i];
                else { std::cerr << "syntax error: trailing backslash\n"; return {}; }
            } else {
                current += c;
            }
        } else if (state == SINGLE) {
            if (c == '\'') state = NORMAL;
            else current += c;
        } else if (state == DOUBLE) {
            if (c == '"') {
                state = NORMAL;
            } else if (c == '\\') {
                if (i + 1 < command.size()) {
                    char next = command[i + 1];
                    if (next == '"' || next == '\\' || next == '$' || next == '`') {
                        current += next; i++;
                    } else {
                        current += c;
                    }
                }
            } else {
                current += c;
            }
        }
    }

    if (state != NORMAL) { std::cerr << "syntax error: unmatched quote\n"; return {}; }
    if (!current.empty()) args.push_back(current);
    return args;
}

// Stores directories from PATH
std::vector<std::string> path_dirs;

// Loads PATH environment variable directories
void load_path_dirs() {
    char* path = getenv("PATH");
    if (!path) return;
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) path_dirs.push_back(dir);
}

// Finds executable matches for tab completion
std::vector<std::string> find_executable_matches(const std::string& prefix) {
    std::vector<std::string> matches;
    for (const auto& dir : path_dirs) {
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        struct dirent* ent;
        while ((ent = readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name.rfind(prefix, 0) == 0) {
                std::string full = dir + "/" + name;
                if (access(full.c_str(), X_OK) == 0) matches.push_back(name);
            }
        }
        closedir(d);
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    return matches;
}

// Finds longest common prefix among matches
std::string longest_common_prefix(const std::vector<std::string>& matches) {
    if (matches.empty()) return "";
    std::string prefix = matches[0];
    for (size_t i = 1; i < matches.size(); i++) {
        size_t j = 0;
        while (j < prefix.size() && j < matches[i].size() && prefix[j] == matches[i][j]) j++;
        prefix = prefix.substr(0, j);
        if (prefix.empty()) break;
    }
    return prefix;
}

// Tab completion state tracking
static std::string last_completion_prefix;
static int tab_press_count = 0;

// Readline tab completion handler
char** completer(const char* text, int start, int end) {
    std::string prefix(text);
    rl_completion_append_character = '\0';

    if (prefix != last_completion_prefix) {
        tab_press_count = 0;
        last_completion_prefix = prefix;
    }

    auto matches = find_executable_matches(prefix);

    for (const auto& b : builtin_command) {
        if (b.rfind(prefix, 0) == 0) matches.push_back(b);
    }

    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());

    if (matches.empty()) { std::cout << "\x07" << std::flush; return nullptr; }

    if (matches.size() == 1) {
        std::string result = matches[0] + " ";
        char** out = (char**)malloc(2 * sizeof(char*));
        out[0] = strdup(result.c_str());
        out[1] = nullptr;
        tab_press_count = 0;
        last_completion_prefix = result;
        return out;
    }

    std::string lcp = longest_common_prefix(matches);
    if (lcp.size() > prefix.size()) {
        rl_replace_line(lcp.c_str(), 1);
        rl_point = rl_end;
        tab_press_count = 0;
        last_completion_prefix = lcp;
        return nullptr;
    }

    tab_press_count++;

    if (tab_press_count == 1) { std::cout << "\x07" << std::flush; return nullptr; }

    std::cout << "\n";
    for (size_t i = 0; i < matches.size(); i++) {
        std::cout << matches[i];
        if (i + 1 < matches.size()) std::cout << "  ";
    }
    std::cout << "\n$ " << prefix << std::flush;
    tab_press_count = 0;
    return nullptr;
}

// Splits command into pipeline parts separated by '|'
std::vector<std::string> split_pipeline(std::string& command) {
    std::vector<std::string> parts;
    std::string current;
    bool insingle = false, indouble = false;

    for (char c : command) {
        if (c == '\'' && !indouble) insingle = !insingle;
        else if (c == '"' && !insingle) indouble = !indouble;
        else if (c == '|' && !insingle && !indouble) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}
