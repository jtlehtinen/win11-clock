#include "utils.h"
#include <stdio.h>
#include <shellscalingapi.h>

namespace utils {

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
      const Float2 dpi = utils::get_dpi_scale(monitor);

      monitors->push_back(Monitor{ .handle = monitor, .position = position, .size = size, .dpi = dpi });

      return TRUE;
    };

    std::vector<Monitor> result;
    EnumDisplayMonitors(nullptr, nullptr, callback, reinterpret_cast<LPARAM>(&result));
    return result;
  }

  bool save_settings(Settings settings, const std::wstring& filename) {
    FILE* f = _wfopen(filename.c_str(), L"wb");
    if (!f) return false;

    uint8_t state[4] = {
      static_cast<uint8_t>(settings.position),
      static_cast<uint8_t>(settings.on_primary_display),
      static_cast<uint8_t>(settings.long_date),
      static_cast<uint8_t>(settings.long_time)
    };

    bool ok = (fwrite(state, sizeof(state), 1, f) == 1);
    fclose(f);
    return ok;
  }

  Settings load_settings(const std::wstring& filename) {
    FILE* f = _wfopen(filename.c_str(), L"rb");
    if (!f) return Settings{};

    uint8_t state[4] = { };
    bool ok = (fread(state, sizeof(state), 1, f) == 1);
    fclose(f);

    if (!ok) return Settings{ };

    return Settings{
      .position = static_cast<Position>(state[0]),
      .on_primary_display = static_cast<bool>(state[1]),
      .long_date = static_cast<bool>(state[2]),
      .long_time = static_cast<bool>(state[3]),
    };
  }

}
