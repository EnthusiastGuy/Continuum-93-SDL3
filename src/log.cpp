// Rotating log in the per-user data folder (Local, not Roaming: logs belong to the machine).
#include "log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

namespace c93::log {
namespace {

constexpr int kKeep = 5;          // continuum.log + 4 older runs
std::mutex gLock;
FILE* gFile = nullptr;
std::string gPath;

// Per-user, per-machine data folder; logs/ lives under it.
fs::path dataDir() {
#if defined(_WIN32)
    if (const char* p = std::getenv("LOCALAPPDATA")) return fs::path(p) / "Continuum 93";
#elif defined(__APPLE__)
    if (const char* p = std::getenv("HOME")) return fs::path(p) / "Library/Application Support/Continuum 93";
#else   // Linux/BSD: XDG state dir is where logs belong
    if (const char* p = std::getenv("XDG_STATE_HOME"); p && *p) return fs::path(p) / "continuum93";
    if (const char* p = std::getenv("HOME")) return fs::path(p) / ".local/state/continuum93";
#endif
    return fs::current_path();
}

// "<name>.log" -> "<name>.1.log" -> ... ; the oldest falls off the end.
void rotate(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    auto file = [&](int i) { return dir / (i ? name + "." + std::to_string(i) + ".log" : name + ".log"); };
    fs::remove(file(kKeep - 1), ec);
    for (int i = kKeep - 2; i >= 0; --i) fs::rename(file(i), file(i + 1), ec);
}

const char* tag(Level l) { return l == Level::Info ? "INFO " : l == Level::Warn ? "WARN " : "ERROR"; }

} // namespace

void open() {
    std::lock_guard g(gLock);
    if (gFile) return;
    // C93_RUN_TAG keeps two concurrent runs (tests, tools) from rotating each other's log away.
    std::string name = "continuum";
    if (const char* t = std::getenv("C93_RUN_TAG"); t && *t) name += std::string("-") + t;
    std::error_code ec;
    fs::path dir = dataDir() / "logs";
    fs::create_directories(dir, ec);
    rotate(dir, name);
    gPath = (dir / (name + ".log")).string();
    gFile = std::fopen(gPath.c_str(), "w");
    if (!gFile) gPath.clear();
}

void close() {
    std::lock_guard g(gLock);
    if (gFile) std::fclose(gFile);
    gFile = nullptr;
}

const char* path() { return gPath.c_str(); }

void write(Level level, const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    int ms = int(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char line[1100];
    int n = std::snprintf(line, sizeof line, "%02d:%02d:%02d.%03d %s %s\n",
                          tm.tm_hour, tm.tm_min, tm.tm_sec, ms, tag(level), msg);

    std::lock_guard g(gLock);
    std::fwrite(line, 1, size_t(n), stderr);
    if (gFile) {
        std::fwrite(line, 1, size_t(n), gFile);
        std::fflush(gFile); // logs are rare; flushing keeps the last line when we crash
    }
}

} // namespace c93::log
