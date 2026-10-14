#pragma once

#include <string>

// Plain-text diagnostic log. Truncated on open; all calls are no-ops until LogOpen succeeds.
void LogOpen(const std::wstring& path);
void LogClose();
void Log(const char* format, ...);
