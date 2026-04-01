#include "core/StructuredLog.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace structured_log {
namespace {

std::mutex g_mutex;
std::ofstream g_log;
std::atomic<bool> g_initialized{false};

std::string nowIso8601Utc() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::string sanitize(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if (c == '\\' || c == '"') out.push_back('\\');
    if (c == '\n' || c == '\r') {
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

void writeLine(const char* level, const std::string& event, const std::string& message) {
  if (!g_initialized.load() || !g_log.is_open()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  g_log << "{\"ts\":\"" << nowIso8601Utc() << "\","
        << "\"level\":\"" << level << "\","
        << "\"event\":\"" << sanitize(event) << "\","
        << "\"message\":\"" << sanitize(message) << "\"}" << std::endl;
}

void onCrashSignal(int sig) {
  const char* signalName = "UNKNOWN";
  if (sig == SIGSEGV) signalName = "SIGSEGV";
  if (sig == SIGABRT) signalName = "SIGABRT";
  if (sig == SIGFPE) signalName = "SIGFPE";
  if (sig == SIGILL) signalName = "SIGILL";
  writeLine("fatal", "signal", std::string("Process terminated by ") + signalName);
  std::_Exit(128 + sig);
}

void onTerminate() {
  writeLine("fatal", "terminate", "Unhandled exception or std::terminate invoked");
  std::_Exit(134);
}

}  // namespace

void initialize(const std::string& appName) {
  if (g_initialized.load()) return;

  std::error_code ec;
  std::filesystem::create_directories("logs", ec);

  const std::string filename = "logs/" + appName + "-" + nowIso8601Utc() + ".jsonl";
  g_log.open(filename, std::ios::out | std::ios::app);
  g_initialized.store(g_log.is_open());
  if (g_initialized.load()) {
    writeLine("info", "log_init", std::string("Logging initialized at ") + filename);
  }
}

void shutdown() {
  if (!g_initialized.load()) return;
  writeLine("info", "log_shutdown", "Logging shutdown");
  std::lock_guard<std::mutex> lock(g_mutex);
  g_log.flush();
  g_log.close();
  g_initialized.store(false);
}

void info(const std::string& event, const std::string& message) {
  writeLine("info", event, message);
}

void warn(const std::string& event, const std::string& message) {
  writeLine("warn", event, message);
}

void error(const std::string& event, const std::string& message) {
  writeLine("error", event, message);
}

void installCrashHandlers() {
  std::set_terminate(onTerminate);
  std::signal(SIGSEGV, onCrashSignal);
  std::signal(SIGABRT, onCrashSignal);
  std::signal(SIGFPE, onCrashSignal);
  std::signal(SIGILL, onCrashSignal);
}

}  // namespace structured_log
