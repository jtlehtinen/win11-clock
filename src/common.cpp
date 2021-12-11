#include "common.h"
#include <stdio.h>
#include <dwmapi.h>
#include <shellscalingapi.h>

namespace {
  namespace registry {
    DWORD read_dword(const std::wstring& subkey, const std::wstring& value) {
      DWORD result = 0;
      DWORD size = static_cast<DWORD>(sizeof(result));
      if (RegGetValueW(HKEY_CURRENT_USER, subkey.c_str(), value.c_str(), RRF_RT_DWORD, nullptr, &result, &size) != ERROR_SUCCESS) return 0;

      return result;
    }
  }
}

bool Settings::load(const std::wstring& filename) {
  FILE* f = _wfopen(filename.c_str(), L"rb");
  if (!f) return false;

  Settings settings;
  bool ok = (fread(&settings, sizeof(settings), 1, f) == 1);
  if (ok) *this = settings;

  fclose(f);
  return ok;
}

bool Settings::save(const std::wstring& filename) {
  FILE* f = _wfopen(filename.c_str(), L"wb");
  if (!f) return false;

  bool ok = (fwrite(this, sizeof(*this), 1, f) == 1);

  fclose(f);
  return ok;
}

namespace common {
  std::wstring get_temp_directory() {
    wchar_t buffer[MAX_PATH + 1];
    if (GetTempPathW(MAX_PATH + 1, buffer) == 0) return L"";

    return buffer + std::wstring(L"Win11Clock\\");
  }

  std::wstring get_user_default_locale_name() {
    wchar_t buffer[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(buffer, static_cast<int>(std::size(buffer))) == 0) return L"";
    return buffer;
  }

  std::wstring get_date_format(const std::wstring& locale, DWORD format_flag) {
    auto callback = [](LPWSTR format_string, CALID calendar_id, LPARAM lparam) -> BOOL {
      *reinterpret_cast<std::wstring*>(lparam) = format_string;
      return FALSE; // @NOTE: We only care about the first one.
    };

    std::wstring result;
    EnumDateFormatsExEx(callback, locale.c_str(), format_flag, reinterpret_cast<LPARAM>(&result));
    return result;
  }

  std::wstring get_time_format(const std::wstring& locale, DWORD format_flag) {
    auto callback = [](LPWSTR format_string, LPARAM lparam) -> BOOL {
      *reinterpret_cast<std::wstring*>(lparam) = format_string;
      return FALSE; // @NOTE: We only care about the first one.
    };

    std::wstring result;
    EnumTimeFormatsEx(callback, locale.c_str(), format_flag, reinterpret_cast<LPARAM>(&result));
    return result;
  }

  std::wstring format_date(SYSTEMTIME time, const std::wstring& locale, const std::wstring& date_format) {
    constexpr int stack_buffer_size = 128;
    wchar_t stack_buffer[stack_buffer_size];

    if (GetDateFormatEx(locale.c_str(), 0, &time, date_format.c_str(), stack_buffer, stack_buffer_size, nullptr) != 0)
      return stack_buffer;

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      int required = GetDateFormatEx(locale.c_str(), 0, &time, date_format.c_str(), nullptr, 0, nullptr);
      int required_no_null = required - 1;

      std::wstring buffer(static_cast<size_t>(required_no_null), '\0'); // @NOTE: Since C++11 always allocates +1 for null
      GetDateFormatEx(locale.c_str(), 0, &time, date_format.c_str(), buffer.data(), required, nullptr);

      return buffer;
    }

    return L"";
  }

  std::wstring format_time(SYSTEMTIME time, const std::wstring& locale, const std::wstring& time_format) {
    constexpr int stack_buffer_size = 128;
    wchar_t stack_buffer[stack_buffer_size];

    if (GetTimeFormatEx(locale.c_str(), 0, &time, time_format.c_str(), stack_buffer, stack_buffer_size) != 0)
      return stack_buffer;

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      int required = GetTimeFormatEx(locale.c_str(), 0, &time, time_format.c_str(), nullptr, 0);
      int required_no_null = required - 1;

      std::wstring buffer(static_cast<size_t>(required_no_null), '\0'); // @NOTE: Since C++11 always allocates +1 for null
      GetTimeFormatEx(locale.c_str(), 0, &time, time_format.c_str(), buffer.data(), required);

      return buffer;
    }

    return L"";
  }

  Int2 window_client_size(HWND window) {
    RECT r;
    GetClientRect(window, &r);
    return {r.right - r.left, r.bottom - r.top};
  }

  Float2 get_dpi_scale(HMONITOR monitor) {
    UINT dpix, dpiy;
    if (GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpix, &dpiy) != S_OK) return {1.0f, 1.0f};

    return {static_cast<float>(dpix) / 96.0f, static_cast<float>(dpiy) / 96.0f};
  }

  std::vector<Monitor> get_display_monitors() {
    auto callback = [](HMONITOR monitor, HDC dc, LPRECT rect, LPARAM lparam) {
      auto monitors = reinterpret_cast<std::vector<Monitor>*>(lparam);

      const Int2 position = { rect->left, rect->top };
      const Int2 size = { rect->right - rect->left, rect->bottom - rect->top };
      const Float2 dpi = common::get_dpi_scale(monitor);

      monitors->push_back(Monitor{ .handle = monitor, .position = position, .size = size, .dpi = dpi });

      return TRUE;
    };

    std::vector<Monitor> result;
    EnumDisplayMonitors(nullptr, nullptr, callback, reinterpret_cast<LPARAM>(&result));
    return result;
  }

  Int2 compute_clock_window_size(Float2 dpi) {
    constexpr float base_width = 205.0f;
    constexpr float base_height = 48.0f;
    return {static_cast<int>(base_width * dpi.x + 0.5f), static_cast<int>(base_height * dpi.y + 0.5f)};
  }

  Int2 compute_clock_window_position(Int2 window_size, Int2 monitor_position, Int2 monitor_size, Corner corner) {
    if (corner == Corner::BottomLeft)
      return {monitor_position.x, monitor_position.y + monitor_size.y - window_size.y};

    if (corner == Corner::BottomRight)
      return {monitor_position.x + monitor_size.x - window_size.x, monitor_position.y + monitor_size.y - window_size.y};

    if (corner == Corner::TopLeft)
      return {monitor_position.x, monitor_position.y};

    return {monitor_position.x + monitor_size.x - window_size.x, monitor_position.y};
  }

  std::vector<HWND> get_desktop_windows() {
    auto callback = [](HWND window, LPARAM lparam) -> BOOL {
      reinterpret_cast<std::vector<HWND>*>(lparam)->push_back(window);
      return TRUE;
    };

    std::vector<HWND> result;
    result.reserve(512);
    EnumDesktopWindows(nullptr, callback, reinterpret_cast<LPARAM>(&result));
    return result;
  }

  bool monitor_has_fullscreen_window(HMONITOR monitor, const std::vector<HWND>& windows) {
    MONITORINFO info = {.cbSize = sizeof(MONITORINFO)};
    if (GetMonitorInfo(monitor, &info) == 0) return false;

    for (HWND window : windows) {
      RECT wr;
      if (DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &wr, static_cast<DWORD>(sizeof(wr))) == S_OK) {
        if (EqualRect(&info.rcMonitor, &wr)) return true;
      }
    }

    return false;
  }

  bool read_use_light_theme_from_registry() {
    return registry::read_dword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme") == 1;
  }

  void open_region_control_panel() {
    ShellExecuteW(nullptr, L"open", L"control.exe", L"/name Microsoft.RegionAndLanguage", nullptr, SW_SHOW);
  }

  void exit_with_error_message(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"win11 clock", MB_ICONERROR);
    ExitProcess(1);
  }
}
