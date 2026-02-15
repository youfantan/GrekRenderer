#include <iostream>
#include <fstream>
#include <ctime>
#include <format>

static std::ofstream g_log_file;

#define CLR_INFO  "\033[32m"
#define CLR_DEBUG "\033[36m"
#define CLR_ERROR "\033[31m"
#define CLR_RESET "\033[0m"

inline const char* now_time() {
    static char buf[32];
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::snprintf(buf, sizeof(buf), "%d/%d/%d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return buf;
}

inline void log_init(const char* filename) {
    g_log_file.open(filename, std::ios::app);
}

inline void log_close() {
    if (g_log_file.is_open())
        g_log_file.close();
}

template<typename... Args>
inline void log_print(const char* level, const char* color, const char* file_name, const char* func_name, int col, std::string fmt, Args&&... args) {
    auto msg = std::vformat(fmt, std::make_format_args(args...));
    auto line = std::format("[{}][{}][{}({}:{})] {}", level, now_time(), func_name, file_name, col, msg);
    std::cout << color << line << CLR_RESET << '\n';
    if (g_log_file.is_open()) {
        g_log_file << line << '\n';
        g_log_file.flush();
    }
}

#define LOG_INFO(...)  log_print("INFO",  CLR_INFO, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) log_print("DEBUG", CLR_DEBUG, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) log_print("ERROR", CLR_ERROR, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)