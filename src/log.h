// Logging: one line per event, "Subsystem: what happened[; consequence]".
// See docs/LOGGING.md for the rules. Thread safe; the CPU thread logs too.
#pragma once

namespace c93::log {

enum class Level { Info, Warn, Error };

// Opens <user data>/logs/continuum.log, rotating older runs. Safe to call once.
void open();
void close();
void write(Level level, const char* fmt, ...);
const char* path(); // where the log file lives (empty if it could not be opened)

} // namespace c93::log

// Message starts with the subsystem name: LOGI("Video: %d layers", n)
#define LOGI(...) ::c93::log::write(::c93::log::Level::Info, __VA_ARGS__)
#define LOGW(...) ::c93::log::write(::c93::log::Level::Warn, __VA_ARGS__)
#define LOGE(...) ::c93::log::write(::c93::log::Level::Error, __VA_ARGS__)
