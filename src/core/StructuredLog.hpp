#pragma once

#include <string>

namespace structured_log {

void initialize(const std::string& appName);
void shutdown();

void info(const std::string& event, const std::string& message);
void warn(const std::string& event, const std::string& message);
void error(const std::string& event, const std::string& message);

void installCrashHandlers();

}  // namespace structured_log
