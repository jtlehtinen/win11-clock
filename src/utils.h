#pragma once

// @TODO: ugh, windows.h include in a header..
#include <windows.h>
#include <string>
#include <vector>

struct Int2 { int x, y; };

struct Float2 { float x, y; };

struct Monitor {
  HMONITOR handle = nullptr;
  Int2 position = { };
  Int2 size = { };
  Float2 dpi = { };

  bool is_primary() const {
    // https://devblogs.microsoft.com/oldnewthing/20070809-00/?p=25643
    return position.x == 0 && position.y == 0;
  }
};

enum Position : uint8_t {
  TopRight = 1,
  TopLeft = 2,
  BottomRight = 4,
  BottomLeft = 8,
};

inline bool is_left(Position position) { return (position == Position::BottomLeft) || (position == Position::TopLeft); }
inline bool is_right(Position position) { return !is_left(position); }

struct Settings {
  Position position = Position::BottomRight;
  bool on_primary_display = false;
  bool long_date = false;
  bool long_time = false;
};

namespace utils {

  std::wstring get_temp_directory();

  std::wstring get_user_default_locale_name();
  std::wstring get_date_format(const std::wstring& locale, DWORD format_flag);
  std::wstring get_time_format(const std::wstring& locale, DWORD format_flag);
  std::wstring format_date(SYSTEMTIME time, const std::wstring& locale, const std::wstring& date_format);
  std::wstring format_time(SYSTEMTIME time, const std::wstring& locale, const std::wstring& time_format);

  Int2 window_client_size(HWND window);

  Float2 get_dpi_scale(HMONITOR monitor);
  std::vector<Monitor> get_display_monitors();

  bool save_settings(Settings settings, const std::wstring& filename);
  Settings load_settings(const std::wstring& filename);

}
