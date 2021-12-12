#pragma once

// @TODO: ugh, windows.h include in a header..
#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

struct Int2 { int x, y; };

struct Float2 { float x, y; };

enum Corner : uint8_t {
  BottomLeft = 0,
  BottomRight = 1,
  TopLeft = 2,
  TopRight = 3,
};

inline bool is_left(Corner corner) { return (corner == Corner::BottomLeft) || (corner == Corner::TopLeft); }
inline bool is_right(Corner corner) { return !is_left(corner); }

struct Settings {
  Corner corner = Corner::BottomRight;
  bool long_date = false;
  bool long_time = false;
  bool on_primary_display = false;
  bool on_fullscreen = false;
};

static_assert(sizeof(Settings) == 5);

Settings load_settings(const std::wstring& filename);
bool save_settings(const std::wstring& filename, Settings settings);

inline bool operator ==(Settings lhs, Settings rhs) { return memcmp(&lhs, &rhs, sizeof(Settings)) == 0; }
inline bool operator !=(Settings lhs, Settings rhs) { return !(lhs == rhs); }

struct Monitor {
  HMONITOR handle = nullptr;
  Int2 position = { };
  Int2 size = { };
  Float2 dpi = {1.0f, 1.0f};
};

// https://devblogs.microsoft.com/oldnewthing/20070809-00/?p=25643
inline bool is_primary_monitor(const Monitor& monitor) { return (monitor.position.x == 0) && (monitor.position.y == 0); }

namespace common {
  std::wstring get_temp_directory();

  std::wstring get_user_default_locale_name();
  std::wstring get_date_format(const std::wstring& locale, DWORD format_flag);
  std::wstring get_time_format(const std::wstring& locale, DWORD format_flag);
  std::wstring format_date(SYSTEMTIME time, const std::wstring& locale, const std::wstring& date_format);
  std::wstring format_time(SYSTEMTIME time, const std::wstring& locale, const std::wstring& time_format);

  Float2 get_dpi_scale(HMONITOR monitor);
  std::vector<Monitor> get_display_monitors();

  Int2 window_client_size(HWND window);
  Int2 compute_clock_window_size(Float2 dpi);
  Int2 compute_clock_window_position(Int2 window_size, Int2 monitor_position, Int2 monitor_size, Corner corner);

  std::vector<HWND> get_desktop_windows();
  bool monitor_has_fullscreen_window(HMONITOR monitor, const std::vector<HWND>& windows);

  bool read_use_light_theme_from_registry();
  void open_region_control_panel();

  void exit_with_error_message(const std::wstring& message);
}
