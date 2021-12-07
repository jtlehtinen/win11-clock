// @TODO: clean this mess
// @TODO: error handling
// @TODO: timer 1 second or 1 minute
// @TODO: slow startup
// @TODO: settings localization

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

#include "common.h"
#include <bitset>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <d2d1.h>
#include <dwrite.h>

constexpr UINT WM_CLOCK_NOTIFY_COMMAND = (WM_USER + 1);

namespace registry {
  DWORD read_dword(const std::wstring& subkey, const std::wstring& value) {
    DWORD result = 0;
    DWORD size = static_cast<DWORD>(sizeof(result));
    if (RegGetValueW(HKEY_CURRENT_USER, subkey.c_str(), value.c_str(), RRF_RT_DWORD, nullptr, &result, &size) != ERROR_SUCCESS) return 0;

    return result;
  }
}

enum AppFlags : uint32_t {
  kAppFlagRecreateRequested = 0,
  kAppFlagColorModeChanged = 1,
  kAppFlagLanguageOrRegionChanged = 2,
  kAppFlagUseLightTheme = 3,
};

struct DateTimeFormat {
  std::wstring locale;
  std::wstring short_date;
  std::wstring long_date;
  std::wstring short_time;
  std::wstring long_time;
};

struct DateTime {
  std::wstring short_date;
  std::wstring short_time;
  std::wstring long_date;
  std::wstring long_time;
};

struct ClockWindow {
  HWND window = nullptr;
  ID2D1DCRenderTarget* rt = nullptr;
  ID2D1SolidColorBrush* brush = nullptr;
  HDC memory_dc = nullptr;
  HBITMAP bitmap = nullptr;
};

struct TextFormat {
  IDWriteTextFormat* text_format = nullptr;
  Float2 for_dpi = {1.0f, 1.0f};
};

struct App {
  DateTimeFormat format;
  DateTime datetime;
  Settings settings;

  std::vector<Monitor> monitors;
  std::vector<ClockWindow> clocks;
  HWND dummy_window = nullptr;

  ID2D1Factory* d2d = nullptr;
  IDWriteFactory* dwrite = nullptr;
  std::vector<TextFormat> text_formats;

  std::bitset<8> flags; // see AppFlags
};

bool read_use_light_theme_from_registry() {
  return registry::read_dword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme") == 1;
}

uint32_t find_clock_index(const App& app, HWND window) {
  for (size_t i = 0; i < app.clocks.size(); ++i) {
    if (app.clocks[i].window == window) return static_cast<uint32_t>(i);
  }
  return UINT32_MAX;
}

DWRITE_TEXT_ALIGNMENT get_text_alignment_for(Corner corner) {
  if (corner == Corner::BottomLeft) return DWRITE_TEXT_ALIGNMENT_LEADING;
  if (corner == Corner::BottomRight) return DWRITE_TEXT_ALIGNMENT_TRAILING;
  if (corner == Corner::TopLeft) return DWRITE_TEXT_ALIGNMENT_LEADING;
  if (corner == Corner::TopRight) return DWRITE_TEXT_ALIGNMENT_TRAILING;
  return DWRITE_TEXT_ALIGNMENT_LEADING;
}

bool create_text_formats(App& app) {
  for (const Monitor& monitor : app.monitors) {
    float fontsize = monitor.dpi.x * 12.0f;

    IDWriteTextFormat* format = nullptr;
    app.dwrite->CreateTextFormat(L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontsize, app.format.locale.c_str(), &format);
    format->SetTextAlignment(get_text_alignment_for(app.settings.corner));
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    app.text_formats.push_back(TextFormat{format, monitor.dpi});
  }

  return true;
}

void destroy_text_formats(App& app) {
  for (TextFormat& format : app.text_formats) {
    format.text_format->Release();
  }
  app.text_formats.clear();
}

TextFormat find_text_format(const App& app, Float2 dpi) {
  for (const TextFormat& format : app.text_formats) {
    if (format.for_dpi.x == dpi.x) return format;
  }

  return app.text_formats.front();
}

ClockWindow create_clock_window(HINSTANCE instance, const Monitor& monitor, Corner corner, ID2D1Factory* d2d, App* app) {
  constexpr DWORD window_style = WS_POPUP;
  constexpr DWORD extended_window_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT;
  HWND window = CreateWindowExW(extended_window_style, L"clock-class", L"", window_style, 0, 0, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, nullptr);
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));

  const bool show = !monitor.is_primary() || (monitor.is_primary() && app->settings.primary_display);
  if (show) ShowWindow(window, SW_SHOW);

  const Int2 size = common::compute_clock_window_size(monitor.dpi);
  const Int2 position = common::compute_clock_window_position(size, monitor.position, monitor.size, corner);
  SetWindowPos(window, HWND_TOPMOST, position.x, position.y, size.x, size.y, SWP_NOACTIVATE);

  HDC window_dc = GetDC(window);
  HDC memory_dc = CreateCompatibleDC(window_dc);
  HBITMAP bitmap = CreateCompatibleBitmap(window_dc, size.x, size.y);
  SelectObject(memory_dc, bitmap);
  ReleaseDC(window, window_dc);

  D2D1_RENDER_TARGET_PROPERTIES props = { };
  props.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
  props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
  props.dpiX = 96.0f;
  props.dpiY = 96.0f;
  props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
  props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

  ID2D1DCRenderTarget* rt = nullptr;
  d2d->CreateDCRenderTarget(&props, &rt);

  ID2D1SolidColorBrush* brush = nullptr;
  rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &brush);

  return ClockWindow{.window = window, .rt = rt, .brush = brush, .memory_dc = memory_dc, .bitmap = bitmap};
};

void destroy_clock_window(ClockWindow& clock) {
  if (clock.brush) clock.brush->Release();
  if (clock.rt) clock.rt->Release();
  if (clock.memory_dc) DeleteDC(clock.memory_dc);
  if (clock.bitmap) DeleteObject(clock.bitmap);
  if (clock.window) DestroyWindow(clock.window);
  clock = { };
}

uint32_t find_primary_monitor_index(const App& app) {
  const size_t count = app.monitors.size();
  for (size_t i = 0; i < count; ++i) {
    if (app.monitors[i].is_primary()) return static_cast<uint32_t>(i);
  }
  return UINT32_MAX;
}

void request_repaint_for_clock_windows(const App& app) {
  for (const ClockWindow& clock : app.clocks) {
    InvalidateRect(clock.window, nullptr, FALSE);
  }
}

void update_datetime_format(DateTimeFormat& format) {
  format.locale = common::get_user_default_locale_name();
  format.short_date = common::get_date_format(format.locale, DATE_SHORTDATE);
  format.long_date = common::get_date_format(format.locale, DATE_LONGDATE);
  format.short_time = common::get_time_format(format.locale, TIME_NOSECONDS);
  format.long_time = common::get_time_format(format.locale, 0);
}

void update_datetime(DateTime& datetime, const DateTimeFormat& format) {
  SYSTEMTIME time;
  GetLocalTime(&time);
  datetime.short_date = common::format_date(time, format.locale, format.short_date);
  datetime.long_date = common::format_date(time, format.locale, format.long_date);
  datetime.short_time = common::format_time(time, format.locale, format.short_time);
  datetime.long_time = common::format_time(time, format.locale, format.long_time);
}

void change_settings(App* app, Settings new_settings) {
  Settings old_settings = app->settings;
  app->settings = new_settings;

  if (old_settings.corner != new_settings.corner) {
    auto alignment = get_text_alignment_for(app->settings.corner);
    for (TextFormat& text_format : app->text_formats) {
      text_format.text_format->SetTextAlignment(alignment);
    }

    const size_t count = app->clocks.size();
    for (size_t i = 0; i < count; ++i) {
      const Monitor& monitor = app->monitors[i];
      const ClockWindow& clock = app->clocks[i];

      const Int2 size = common::window_client_size(clock.window);
      const Int2 position = common::compute_clock_window_position(size, monitor.position, monitor.size, app->settings.corner);
      SetWindowPos(clock.window, HWND_TOPMOST, position.x, position.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
    }
  }

  request_repaint_for_clock_windows(*app);
}

LRESULT CALLBACK window_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));

  if (app) {
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); // @TODO: too much?

    switch (message) {
      case WM_PAINT: {
        const uint32_t clock_index = find_clock_index(*app, window);
        if (clock_index != UINT32_MAX) {
          ClockWindow& clock = app->clocks[clock_index];
          const Float2 dpi = app->monitors[clock_index].dpi;
          ID2D1DCRenderTarget* rt = clock.rt;

          const Int2 size = common::window_client_size(window);
          RECT bind_rect = {0, 0, size.x, size.y};
          rt->BindDC(clock.memory_dc, &bind_rect);
          rt->BeginDraw();
          rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
          rt->SetTransform(D2D1::IdentityMatrix());
          rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

          const float width = static_cast<float>(size.x);
          const float height = static_cast<float>(size.y);

          #ifdef CLOCK_DEBUG
          clock.brush->SetColor(D2D1_COLOR_F{1.0f, 0.0f, 0.0f, 1.0f});
          rt->DrawLine(D2D_POINT_2F{0.0f, 0.0f}, D2D_POINT_2F{width, 0.0f}, clock.brush);
          rt->DrawLine(D2D_POINT_2F{0.0f, height}, D2D_POINT_2F{width, height}, clock.brush);
          rt->DrawLine(D2D_POINT_2F{0.0f, 0.0f}, D2D_POINT_2F{0.0f, height}, clock.brush);
          rt->DrawLine(D2D_POINT_2F{width, 0.0f}, D2D_POINT_2F{width, height}, clock.brush);
          #endif

          const bool left = is_left(app->settings.corner);
          const float pad_left = left ? 15.0f * dpi.x : 0.0f;
          const float pad_right = !left ? 15.0f * dpi.x : 0.0f;
          D2D1_RECT_F rect = D2D1::RectF(pad_left, 0.0f, width - pad_right, height);

          const std::wstring& time = app->settings.long_time ? app->datetime.long_time : app->datetime.short_time;
          const std::wstring& date = app->settings.long_date ? app->datetime.long_date : app->datetime.short_date;
          const std::wstring datetime = time + L"\n" + date; // @TODO: wasteful

          TextFormat text_format = find_text_format(*app, dpi);

          D2D1_COLOR_F text_color = app->flags.test(kAppFlagUseLightTheme) ? D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 1.0f} : D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f};
          clock.brush->SetColor(text_color);
          rt->DrawText(datetime.c_str(), static_cast<UINT32>(datetime.length()), text_format.text_format, rect, clock.brush);
          rt->EndDraw();

          HDC desktop_dc = GetDC(nullptr);
          POINT source_point = { };
          SIZE windowSize = {size.x, size.y};
          BLENDFUNCTION blend = {.BlendOp = AC_SRC_OVER, .SourceConstantAlpha = 255, .AlphaFormat = AC_SRC_ALPHA};
          UpdateLayeredWindow(window, desktop_dc, nullptr, &windowSize, clock.memory_dc, &source_point, 0, &blend, ULW_ALPHA);
          ReleaseDC(nullptr, desktop_dc);
        }
        ValidateRect(window, nullptr);
        return 0;
      }
    }
  }

  return DefWindowProcW(window, message, wparam, lparam);
}

bool add_notification_area_icon(HWND window) {
  NOTIFYICONDATAW data = { };
  data.cbSize = sizeof(data);
  data.hWnd = window;
  data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  data.uCallbackMessage = WM_CLOCK_NOTIFY_COMMAND;
  data.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
  wcscpy_s(data.szTip, std::size(data.szTip), L"win11-clock");

  return Shell_NotifyIconW(NIM_ADD, &data) == TRUE;
}

void remove_notification_area_icon(HWND window) {
  NOTIFYICONDATAW data = {.cbSize = sizeof(NOTIFYICONDATAW), .hWnd = window};
  Shell_NotifyIconW(NIM_DELETE, &data);
}

LRESULT CALLBACK dummy_window_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_CREATE) {
    add_notification_area_icon(window);
    return 0;
  }

  App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));

  if (app) {
    switch (message) {
      case WM_DESTROY: {
        remove_notification_area_icon(window);
        PostQuitMessage(0);
        return 0;
      }

      case WM_CLOCK_NOTIFY_COMMAND: {
        constexpr UINT kCmdPrimaryDisplay = 1;
        constexpr UINT kCmdPositionBottomLeft = 2;
        constexpr UINT kCmdPositionBottomRight = 3;
        constexpr UINT kCmdPositionTopLeft = 4;
        constexpr UINT kCmdPositionTopRight = 5;
        constexpr UINT kCmdFormatShortDate = 6;
        constexpr UINT kCmdFormatLongDate = 7;
        constexpr UINT kCmdFormatShortTime = 8;
        constexpr UINT kCmdFormatLongTime = 9;
        constexpr UINT kCmdHideFullscreen = 10;
        constexpr UINT kCmdQuit = 255;

        if (lparam == WM_RBUTTONUP) {
          auto checked = [](bool is) -> UINT { return is ? static_cast<UINT>(MF_CHECKED) : static_cast<UINT>(MF_UNCHECKED); };

          HMENU menu = CreatePopupMenu();

          HMENU position_menu = CreatePopupMenu();
          AppendMenuW(position_menu, checked(app->settings.corner == Corner::BottomLeft), kCmdPositionBottomLeft, L"Bottom Left");
          AppendMenuW(position_menu, checked(app->settings.corner == Corner::BottomRight), kCmdPositionBottomRight, L"Bottom Right");
          AppendMenuW(position_menu, checked(app->settings.corner == Corner::TopLeft), kCmdPositionTopLeft, L"Top Left");
          AppendMenuW(position_menu, checked(app->settings.corner == Corner::TopRight), kCmdPositionTopRight, L"Top Right");
          AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(position_menu), L"Position");

          HMENU date_menu = CreatePopupMenu();
          AppendMenuW(date_menu, checked(!app->settings.long_date), kCmdFormatShortDate, app->datetime.short_date.c_str());
          AppendMenuW(date_menu, checked(app->settings.long_date), kCmdFormatLongDate, app->datetime.long_date.c_str());
          AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(date_menu), L"Date Format");

          HMENU time_menu = CreatePopupMenu();
          AppendMenuW(time_menu, checked(!app->settings.long_time), kCmdFormatShortTime, app->datetime.short_time.c_str());
          AppendMenuW(time_menu, checked(app->settings.long_time), kCmdFormatLongTime, app->datetime.long_time.c_str());
          AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(time_menu), L"Time Format");

          AppendMenuW(menu, checked(app->settings.hide_fullscreen), kCmdHideFullscreen, L"Hide Fullscreen");
          AppendMenuW(menu, checked(app->settings.primary_display), kCmdPrimaryDisplay, L"Primary Display");
          AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
          AppendMenuW(menu, MF_STRING, kCmdQuit, L"Exit");

          SetForegroundWindow(window);

          POINT mouse;
          GetCursorPos(&mouse);
          UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, mouse.x, mouse.y, 0, window, nullptr));

          DestroyMenu(menu);
          DestroyMenu(position_menu);
          DestroyMenu(time_menu);
          DestroyMenu(date_menu);

          Settings settings = app->settings;
          switch (cmd) {
            case kCmdQuit: DestroyWindow(window); break;
            case kCmdPrimaryDisplay: settings.primary_display = !settings.primary_display; break;
            case kCmdPositionBottomLeft: settings.corner = Corner::BottomLeft; break;
            case kCmdPositionBottomRight: settings.corner = Corner::BottomRight; break;
            case kCmdPositionTopLeft: settings.corner = Corner::TopLeft; break;
            case kCmdPositionTopRight: settings.corner = Corner::TopRight; break;
            case kCmdFormatLongDate: settings.long_date = true; break;
            case kCmdFormatShortDate: settings.long_date = false; break;
            case kCmdFormatLongTime: settings.long_time = true; break;
            case kCmdFormatShortTime: settings.long_time = false; break;
            case kCmdHideFullscreen: settings.hide_fullscreen = !settings.hide_fullscreen; break;
          }
          if (settings != app->settings) change_settings(app, settings);
        }
        return 0;
      }

      case WM_WININICHANGE: {
        // @NOTE: when mode {Dark, Light} is changed this message is received many times (> 10)
        const wchar_t* name = reinterpret_cast<const wchar_t*>(lparam);
        const bool mode_changed = name && (wcscmp(L"ImmersiveColorSet", name) == 0);
        const bool language_or_region_changed = name && (wcscmp(L"intl", name) == 0);

        if (mode_changed) app->flags.set(kAppFlagColorModeChanged, true);
        if (language_or_region_changed) app->flags.set(kAppFlagLanguageOrRegionChanged, true);
        return 0;
      }

      case WM_DEVICECHANGE: app->flags.set(kAppFlagRecreateRequested); break;
      case WM_DISPLAYCHANGE: app->flags.set(kAppFlagRecreateRequested); break;
      case WM_DPICHANGED: app->flags.set(kAppFlagRecreateRequested); break;
      case WM_INPUTLANGCHANGE: OutputDebugStringA("WM_INPUTLANGCHANGE\n"); break;
      case WM_TIMECHANGE: OutputDebugStringA("WM_TIMECHANGE\n"); break;

      case WM_TIMER: {
        if (app->flags.test(kAppFlagColorModeChanged)) {
          app->flags.reset(kAppFlagColorModeChanged);
          app->flags.set(kAppFlagUseLightTheme, read_use_light_theme_from_registry());
        }

        if (app->flags.test(kAppFlagLanguageOrRegionChanged)) {
          app->flags.reset(kAppFlagLanguageOrRegionChanged);
          update_datetime_format(app->format);
        }

        update_datetime(app->datetime, app->format);
        if (app->flags.test(kAppFlagRecreateRequested)) {
          app->flags.reset(kAppFlagRecreateRequested);

          app->monitors = common::get_display_monitors();

          for (ClockWindow& clock : app->clocks) {
            destroy_clock_window(clock);
          }
          app->clocks.clear();

          HINSTANCE instance = GetModuleHandleW(nullptr);
          for (const Monitor& monitor : app->monitors) {
            app->clocks.push_back(create_clock_window(instance, monitor, app->settings.corner, app->d2d, app));
          }

          destroy_text_formats(*app);
          create_text_formats(*app);
        }

        // @TODO: this is expensive, the desktop window count is in the hunreds
        std::vector<HWND> desktop_windows = common::get_desktop_windows();
        for (size_t i = 0; i < app->clocks.size(); ++i) {
          const Monitor& monitor = app->monitors[i];
          const bool fullscreen = common::monitor_has_fullscreen_window(monitor.handle, desktop_windows);
          const bool hide = (monitor.is_primary() && !app->settings.primary_display) || (fullscreen && app->settings.hide_fullscreen);
          const int show_command = hide ? SW_HIDE : SW_SHOW;
          ShowWindow(app->clocks[i].window, show_command);
        }

        request_repaint_for_clock_windows(*app);
        return 0;
      }
    }
  }

  return DefWindowProcW(window, message, wparam, lparam);
}

App app; // @TODO: ugh... global just for the win_event_hook...

void CALLBACK win_event_hook(HWINEVENTHOOK hook, DWORD event, HWND window, LONG id_object, LONG id_child, DWORD id_event_thread, DWORD event_time) {
  for (const ClockWindow& clock : app.clocks) {
    SetWindowPos(clock.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

int CALLBACK wWinMain(HINSTANCE instance, HINSTANCE ignored, PWSTR command_line, int show_command) {
  const wchar_t* guid = L"6b54d0d4-ac9f-4ce7-b1b4-daa3527c935e";
  HANDLE mutex = CreateMutexW(nullptr, true, guid);
  if (GetLastError() != ERROR_SUCCESS) return 0;

  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  WNDCLASSW dummy_window_class = {.lpfnWndProc = dummy_window_callback, .hInstance = instance, .lpszClassName = L"dummy-class"};
  RegisterClassW(&dummy_window_class);

  WNDCLASSW clock_window_class = {.lpfnWndProc = window_callback, .hInstance = instance, .lpszClassName = L"clock-class"};
  RegisterClassW(&clock_window_class);

  const std::wstring temp_directory = common::get_temp_directory();
  const std::wstring settings_absolute_path = temp_directory + L"settings.dat";
  SHCreateDirectoryExW(nullptr, temp_directory.c_str(), nullptr);
  app.settings.load(settings_absolute_path);

  update_datetime_format(app.format);
  update_datetime(app.datetime, app.format);

  app.dummy_window = CreateWindowExW(WS_EX_TOOLWINDOW, L"dummy-class", L"", 0, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
  if (app.dummy_window) {
    SetWindowLongPtrW(app.dummy_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

    app.flags.set(kAppFlagUseLightTheme, read_use_light_theme_from_registry());
    app.monitors = common::get_display_monitors();

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &app.d2d);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&app.dwrite));
    create_text_formats(app);

    for (const Monitor& monitor : app.monitors) {
      app.clocks.push_back(create_clock_window(instance, monitor, app.settings.corner, app.d2d, &app));
    }

    const UINT_PTR timer = SetTimer(app.dummy_window, 0, 1000, nullptr);
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, win_event_hook, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    MSG msg = { };
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    // @NOTE: windows will do the clean up anyways...
    app.settings.save(settings_absolute_path);

    KillTimer(app.dummy_window, timer);
    UnhookWinEvent(hook);
  }

  ReleaseMutex(mutex);

  return 0;
}
