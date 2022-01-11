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
#include "common.cpp"

constexpr UINT WM_CLOCK_NOTIFY_COMMAND = (WM_USER + 1);

enum AppFlags : uint32_t {
  kAppFlagUseLightTheme = 0,
};

enum TransientAppFlags : uint32_t {
  kTransientAppFlagRecreateRequested = 0,
  kTransientAppFlagColorModeChanged = 1,
  kTransientAppFlagLanguageOrRegionChanged = 2,
  kTransientAppFlagSettingsChanged = 3,
};

struct DateTimeFormat {
  std::wstring locale;
  std::wstring short_date;
  std::wstring long_date;
  std::wstring short_time;
  std::wstring long_time;
};

void update_datetime_format(DateTimeFormat& format) {
  format.locale = common::get_user_default_locale_name();
  format.short_date = common::get_date_format(format.locale, DATE_SHORTDATE);
  format.long_date = common::get_date_format(format.locale, DATE_LONGDATE);
  format.short_time = common::get_time_format(format.locale, TIME_NOSECONDS);
  format.long_time = common::get_time_format(format.locale, 0);
}

struct DateTime {
  std::wstring short_date;
  std::wstring short_time;
  std::wstring long_date;
  std::wstring long_time;
};

void update_datetime(DateTime& datetime, const DateTimeFormat& format) {
  SYSTEMTIME time;
  GetLocalTime(&time);
  datetime.short_date = common::format_date(time, format.locale, format.short_date);
  datetime.long_date = common::format_date(time, format.locale, format.long_date);
  datetime.short_time = common::format_time(time, format.locale, format.short_time);
  datetime.long_time = common::format_time(time, format.locale, format.long_time);
}

struct ClockWindow {
  HWND window = nullptr;
  ID2D1DCRenderTarget* rt = nullptr;
  ID2D1SolidColorBrush* brush = nullptr;
  HDC memory_dc = nullptr;
  HBITMAP bitmap = nullptr;
  IDWriteTextFormat* text_format = nullptr;
  float dpi_scale = 1.0f;
  bool on_primary_monitor = false;
};

struct App {
  DateTimeFormat format;
  DateTime datetime;
  Settings settings;
  std::vector<ClockWindow> clocks;
  ID2D1Factory* d2d = nullptr;
  IDWriteFactory* dwrite = nullptr;
  std::bitset<8> transient_flags; // see TransientAppFlags
  std::bitset<8> flags; // see AppFlags
  std::wstring settings_absolute_path;
};

DWRITE_TEXT_ALIGNMENT get_text_alignment_for(Corner corner) {
  return is_left(corner) ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_TRAILING;
}

ClockWindow create_clock_window(const Monitor& monitor, Corner corner, ID2D1Factory* d2d, App* app) {
  constexpr DWORD window_style = WS_POPUP;
  constexpr DWORD extended_window_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT;
  HWND window = CreateWindowExW(extended_window_style, L"clock-class", L"", window_style, 0, 0, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));

  const Int2 size = common::compute_clock_window_size(monitor.dpi);
  const Int2 position = common::compute_clock_window_position(size, monitor.position, monitor.size, corner);
  const UINT show_flag = (is_primary_monitor(monitor) && !app->settings.on_primary_display) ? static_cast<UINT>(SWP_HIDEWINDOW) : static_cast<UINT>(SWP_SHOWWINDOW);
  SetWindowPos(window, HWND_TOPMOST, position.x, position.y, size.x, size.y, SWP_NOACTIVATE | show_flag);

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

  constexpr float default_font_size = 12.0f;
  float fontsize = monitor.dpi.x * default_font_size;

  IDWriteTextFormat* format = nullptr;
  app->dwrite->CreateTextFormat(L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontsize, app->format.locale.c_str(), &format);
  format->SetTextAlignment(get_text_alignment_for(app->settings.corner));
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

  return ClockWindow{.window = window, .rt = rt, .brush = brush, .memory_dc = memory_dc, .bitmap = bitmap, .text_format = format, .dpi_scale = monitor.dpi.x, .on_primary_monitor = is_primary_monitor(monitor)};
};

void destroy_clock_window(ClockWindow& clock) {
  if (clock.text_format) clock.text_format->Release();
  if (clock.brush) clock.brush->Release();
  if (clock.rt) clock.rt->Release();
  if (clock.memory_dc) DeleteDC(clock.memory_dc);
  if (clock.bitmap) DeleteObject(clock.bitmap);
  if (clock.window) DestroyWindow(clock.window);
  clock = { };
}

const ClockWindow* find_clock_by_hwnd(const App& app, HWND window) {
  for (size_t i = 0; i < app.clocks.size(); ++i) {
    if (app.clocks[i].window == window) return app.clocks.data() + i;
  }
  return nullptr;
}

bool init_direct2d(App& app) {
  return (D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &app.d2d) == S_OK) &&
    (DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&app.dwrite)) == S_OK);
}

void create_clock_windows(App& app) {
  for (const Monitor& monitor : common::get_display_monitors()) {
    app.clocks.push_back(create_clock_window(monitor, app.settings.corner, app.d2d, &app));
  }
}

void destroy_clock_windows(App& app) {
  for (ClockWindow& clock : app.clocks) destroy_clock_window(clock);
  app.clocks.clear();
}

LRESULT CALLBACK window_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA)); app) {
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); // @TODO: too much?
    switch (message) {
      case WM_PAINT: {
        if (const ClockWindow* clock = find_clock_by_hwnd(*app, window); clock) {
          const Int2 size = common::window_client_size(window);
          const float width = static_cast<float>(size.x);
          const float height = static_cast<float>(size.y);

          RECT bind_rect = {0, 0, size.x, size.y};
          clock->rt->BindDC(clock->memory_dc, &bind_rect);
          clock->rt->BeginDraw();
          clock->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
          clock->rt->SetTransform(D2D1::IdentityMatrix());
          clock->rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

          #ifdef CLOCK_DEBUG
          clock->brush->SetColor(D2D1_COLOR_F{1.0f, 0.0f, 0.0f, 1.0f});
          clock->rt->DrawRectangle(D2D1::RectF(0.0f, 0.0f, width, height), clock->brush);
          #endif

          const bool left = is_left(app->settings.corner);
          const float pad_left = left ? 15.0f * clock->dpi_scale : 0.0f;
          const float pad_right = !left ? 15.0f * clock->dpi_scale : 0.0f;
          D2D1_RECT_F rect = D2D1::RectF(pad_left, 0.0f, width - pad_right, height);

          const std::wstring& time = app->settings.long_time ? app->datetime.long_time : app->datetime.short_time;
          const std::wstring& date = app->settings.long_date ? app->datetime.long_date : app->datetime.short_date;
          const std::wstring datetime = time + L"\n" + date; // @TODO: wasteful

          clock->brush->SetColor(app->flags.test(kAppFlagUseLightTheme) ? D2D1_COLOR_F{0.0f, 0.0f, 0.0f, 1.0f} : D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 1.0f});
          clock->rt->DrawText(datetime.c_str(), static_cast<UINT32>(datetime.length()), clock->text_format, rect, clock->brush);

          const bool presentationError = (clock->rt->EndDraw() == D2DERR_RECREATE_TARGET);
          if (presentationError) app->transient_flags.set(kTransientAppFlagRecreateRequested);

          HDC desktop_dc = GetDC(nullptr);
          POINT source_point = { };
          SIZE windowSize = {size.x, size.y};
          BLENDFUNCTION blend = {.BlendOp = AC_SRC_OVER, .SourceConstantAlpha = 255, .AlphaFormat = AC_SRC_ALPHA};
          UpdateLayeredWindow(window, desktop_dc, nullptr, &windowSize, clock->memory_dc, &source_point, 0, &blend, ULW_ALPHA);
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

  if (App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA)); app) {
    switch (message) {
      case WM_DESTROY: {
        remove_notification_area_icon(window);
        PostQuitMessage(0);
        return 0;
      }

      case WM_CLOCK_NOTIFY_COMMAND: {
        if (lparam == WM_RBUTTONUP) {
          constexpr UINT kCmdPrimaryDisplay = 1;
          constexpr UINT kCmdPositionBottomLeft = 2;
          constexpr UINT kCmdPositionBottomRight = 3;
          constexpr UINT kCmdPositionTopLeft = 4;
          constexpr UINT kCmdPositionTopRight = 5;
          constexpr UINT kCmdFormatShortDate = 6;
          constexpr UINT kCmdFormatLongDate = 7;
          constexpr UINT kCmdFormatShortTime = 8;
          constexpr UINT kCmdFormatLongTime = 9;
          constexpr UINT kCmdOnFullscreen = 10;
          constexpr UINT kCmdOpenRegionControlPanel = 11;
          constexpr UINT kCmdQuit = 255;

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

          AppendMenuW(menu, checked(app->settings.on_fullscreen), kCmdOnFullscreen, L"On Fullscreen");
          AppendMenuW(menu, checked(app->settings.on_primary_display), kCmdPrimaryDisplay, L"Primary Display");
          AppendMenuW(menu, MF_STRING, kCmdOpenRegionControlPanel, L"Open Region Options");
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
            case kCmdPrimaryDisplay: settings.on_primary_display = !settings.on_primary_display; break;
            case kCmdPositionBottomLeft: settings.corner = Corner::BottomLeft; break;
            case kCmdPositionBottomRight: settings.corner = Corner::BottomRight; break;
            case kCmdPositionTopLeft: settings.corner = Corner::TopLeft; break;
            case kCmdPositionTopRight: settings.corner = Corner::TopRight; break;
            case kCmdFormatLongDate: settings.long_date = true; break;
            case kCmdFormatShortDate: settings.long_date = false; break;
            case kCmdFormatLongTime: settings.long_time = true; break;
            case kCmdFormatShortTime: settings.long_time = false; break;
            case kCmdOnFullscreen: settings.on_fullscreen = !settings.on_fullscreen; break;
            case kCmdOpenRegionControlPanel: common::open_region_control_panel(); break;
          }
          if (settings != app->settings) {
            app->settings = settings;
            app->transient_flags.set(kTransientAppFlagSettingsChanged);
            app->transient_flags.set(kTransientAppFlagRecreateRequested);
          }
        }
        return 0;
      }

      case WM_WININICHANGE: {
        const wchar_t* name = reinterpret_cast<const wchar_t*>(lparam);
        if (name && wcscmp(L"ImmersiveColorSet", name) == 0) app->transient_flags.set(kTransientAppFlagColorModeChanged, true);
        if (name && wcscmp(L"intl", name) == 0) app->transient_flags.set(kTransientAppFlagLanguageOrRegionChanged, true);
        return 0;
      }

      case WM_DEVICECHANGE:  app->transient_flags.set(kTransientAppFlagRecreateRequested); break;
      case WM_DISPLAYCHANGE: app->transient_flags.set(kTransientAppFlagRecreateRequested); break;
      case WM_DPICHANGED: app->transient_flags.set(kTransientAppFlagRecreateRequested); break;
      case WM_INPUTLANGCHANGE: OutputDebugStringA("WM_INPUTLANGCHANGE\n"); break;
      case WM_TIMECHANGE: OutputDebugStringA("WM_TIMECHANGE\n"); break;

      case WM_TIMER: {
        if (app->transient_flags.test(kTransientAppFlagColorModeChanged)) app->flags.set(kAppFlagUseLightTheme, common::read_use_light_theme_from_registry());
        if (app->transient_flags.test(kTransientAppFlagLanguageOrRegionChanged)) update_datetime_format(app->format);
        if (app->transient_flags.test(kTransientAppFlagSettingsChanged)) save_settings(app->settings_absolute_path, app->settings);
        if (app->transient_flags.test(kTransientAppFlagRecreateRequested)) {
          destroy_clock_windows(*app);
          create_clock_windows(*app);
        }

        app->transient_flags.reset();
        update_datetime(app->datetime, app->format);

        // @TODO: this is expensive, the desktop window count is in the hunreds
        std::vector<HWND> desktop_windows = common::get_desktop_windows();
        for (const ClockWindow& clock : app->clocks) {
          if (HMONITOR monitor = MonitorFromWindow(clock.window, MONITOR_DEFAULTTONULL); monitor) {
            const bool fullscreen = common::monitor_has_fullscreen_window(monitor, desktop_windows);
            const bool hide = (clock.on_primary_monitor && !app->settings.on_primary_display) || (fullscreen && !app->settings.on_fullscreen);
            ShowWindow(clock.window, hide ? SW_HIDE : SW_SHOW);
          }
          InvalidateRect(clock.window, nullptr, FALSE);
        }
        return 0;
      }
    }
  }

  return DefWindowProcW(window, message, wparam, lparam);
}

App app; // @TODO: ugh... global just for the win_event_hook...

void CALLBACK win_event_hook(HWINEVENTHOOK hook, DWORD event, HWND window, LONG id_object, LONG id_child, DWORD id_event_thread, DWORD event_time) {
  for (const ClockWindow& clock : app.clocks)
    SetWindowPos(clock.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
  SHCreateDirectoryExW(nullptr, temp_directory.c_str(), nullptr);

  app.settings_absolute_path = temp_directory + L"settings.dat";
  app.settings = load_settings(app.settings_absolute_path);

  if (HWND dummy_window = CreateWindowExW(WS_EX_TOOLWINDOW, L"dummy-class", L"", 0, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr); dummy_window) {
    SetWindowLongPtrW(dummy_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

    if (init_direct2d(app)) {
      update_datetime_format(app.format);
      update_datetime(app.datetime, app.format);

      app.flags.set(kAppFlagUseLightTheme, common::read_use_light_theme_from_registry());
      create_clock_windows(app);

      const UINT_PTR timer = SetTimer(dummy_window, 0, 1000, nullptr);
      HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, win_event_hook, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
      for (const ClockWindow& clock : app.clocks) InvalidateRect(clock.window, nullptr, FALSE);

      MSG msg = { };
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }

      save_settings(app.settings_absolute_path, app.settings);
      KillTimer(dummy_window, timer);
      UnhookWinEvent(hook);
    }
  }
  ReleaseMutex(mutex);

  return 0;
}
