// @TODO: clean this mess
// @TODO: error handling
// @TODO: handle WM_DISPLAYCHANGE, WM_DPICHANGED, WM_INPUTLANGCHANGE, WM_DEVICECHANGE, WM_TIMECHANGE?
// @TODO: hide when fullscreen window?
// @TODO: timer should be one minute or one second depending on the displayed time format

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include "utils.h"

#include <windows.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <dwrite.h>
#include <d2d1.h>

#include <stdint.h>
#include <string>
#include <vector>

constexpr UINT WM_CLOCK_NOTIFY_COMMAND = (WM_USER + 1);

struct DateTimeFormat {
  std::wstring locale;
  std::wstring short_date;
  std::wstring long_date;
  std::wstring short_time;
  std::wstring long_time;
};

struct DeviceDependentResources {
  ID2D1HwndRenderTarget* rt = nullptr;
  ID2D1SolidColorBrush* white_brush = nullptr;
  ID2D1SolidColorBrush* red_brush = nullptr;
  ID2D1SolidColorBrush* green_brush = nullptr;
};

struct App {
  DateTimeFormat format;
  Settings settings;
  std::wstring short_date;
  std::wstring short_time;
  std::wstring long_date;
  std::wstring long_time;

  std::vector<Monitor> monitors;

  std::vector<HWND> clock_windows;
  std::vector<DeviceDependentResources> device_dependent_resources;
  HWND dummy_window = nullptr;

  ID2D1Factory* d2d_factory = nullptr;
  IDWriteFactory* dwrite_factory = nullptr;
  IDWriteTextFormat* text_format = nullptr;
};

uint32_t find_window_index(const App& app, HWND window) {
  const size_t count = app.clock_windows.size();
  for (size_t i = 0; i < count; ++i) {
    if (window == app.clock_windows[i]) return static_cast<uint32_t>(i);
  }

  return UINT32_MAX;
}

DWRITE_TEXT_ALIGNMENT text_alignment_from_clock_position(Position position) {
  if (position == Position::BottomLeft) return DWRITE_TEXT_ALIGNMENT_LEADING;
  if (position == Position::BottomRight) return DWRITE_TEXT_ALIGNMENT_TRAILING;
  if (position == Position::TopLeft) return DWRITE_TEXT_ALIGNMENT_LEADING;
  if (position == Position::TopRight) return DWRITE_TEXT_ALIGNMENT_TRAILING;

  return DWRITE_TEXT_ALIGNMENT_LEADING;
}

uint32_t find_primary_monitor_index(const App& app) {
  const size_t count = app.monitors.size();
  for (size_t i = 0; i < count; ++i) {
    if (app.monitors[i].is_primary()) return static_cast<uint32_t>(i);
  }
  return UINT32_MAX;
}

Int2 compute_clock_window_size(Float2 dpi) {
  constexpr float base_width = 200.0f;
  constexpr float base_height = 48.0f;
  return {static_cast<int>(base_width * dpi.x + 0.5f), static_cast<int>(base_height * dpi.y + 0.5f)};
}

Int2 compute_clock_window_position(Int2 window_size, Int2 monitor_position, Int2 monitor_size, Position position) {
  if (position == Position::BottomLeft)
    return {monitor_position.x, monitor_position.y + monitor_size.y - window_size.y};

  if (position == Position::BottomRight)
    return {monitor_position.x + monitor_size.x - window_size.x, monitor_position.y + monitor_size.y - window_size.y};

  if (position == Position::TopLeft)
    return {monitor_position.x, monitor_position.y};

  return {monitor_position.x + monitor_size.x - window_size.x, monitor_position.y};
}

void settings_changed(App* app) {
  const size_t count = app->clock_windows.size();

  for (size_t i = 0; i < count; ++i) {
    HWND window = app->clock_windows[i];
    const Monitor& monitor = app->monitors[i];

    const Int2 size = utils::window_client_size(window);
    const Int2 position = compute_clock_window_position(size, monitor.position, monitor.size, app->settings.position);
    SetWindowPos(window, HWND_TOPMOST, position.x, position.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE);
  }

  SYSTEMTIME time;
  GetLocalTime(&time);
  app->short_date = utils::format_date(time, app->format.locale, app->format.short_date);
  app->short_time = utils::format_time(time, app->format.locale, app->format.short_time);
  app->long_date = utils::format_date(time, app->format.locale, app->format.long_date);
  app->long_time = utils::format_time(time, app->format.locale, app->format.long_time);

  for (HWND window : app->clock_windows) {
    InvalidateRect(window, nullptr, FALSE);
  }
}

LRESULT CALLBACK window_callback(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));

  if (app) {
    // @TODO: too much?
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    switch (message) {
      case WM_PAINT: {
        const uint32_t idx = find_window_index(*app, window);
        if (idx != UINT32_MAX) {
          ID2D1HwndRenderTarget* rt = app->device_dependent_resources[idx].rt;
          ID2D1SolidColorBrush* white_brush = app->device_dependent_resources[idx].white_brush;

          auto alignment = text_alignment_from_clock_position(app->settings.position);
          if (alignment != app->text_format->GetTextAlignment()) {
            app->text_format->SetTextAlignment(alignment);
          }

          rt->BeginDraw();
          rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
          rt->SetTransform(D2D1::IdentityMatrix());
          rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

          const Int2 size = utils::window_client_size(window);
          const Float2 dpi = app->monitors[idx].dpi;
          const Float2 sizef = { float(size.x) / dpi.x, float(size.y) / dpi.y };

          #ifdef CLOCK_DEBUG
          ID2D1SolidColorBrush* red_brush = app->device_dependent_resources[idx].red_brush;
          ID2D1SolidColorBrush* green_brush = app->device_dependent_resources[idx].green_brush;
          rt->DrawLine(D2D_POINT_2F{ 0.0f, 0.0f }, D2D_POINT_2F{ sizef.x, 0.0f }, red_brush);
          rt->DrawLine(D2D_POINT_2F{ 0.0f, sizef.y }, D2D_POINT_2F{ sizef.x, sizef.y }, green_brush);
          rt->DrawLine(D2D_POINT_2F{ 0.0f, 0.0f }, D2D_POINT_2F{ 0.0f, sizef.y }, red_brush);
          rt->DrawLine(D2D_POINT_2F{ sizef.x, 0.0f }, D2D_POINT_2F{ sizef.x, sizef.y }, green_brush);
          #endif

          const Position clock_position = app->settings.position;
          const float pad_left = is_left(clock_position) ? 28.0f / dpi.x : 0.0f;
          const float pad_right = is_right(clock_position) ? 28.0f / dpi.x : 0.0f;

          D2D1_RECT_F layout_rect = D2D1::RectF(pad_left, 0.0f, sizef.x - pad_right, sizef.y);

          const std::wstring& time = app->settings.long_time ? app->long_time : app->short_time;
          const std::wstring& date = app->settings.long_date ? app->long_date : app->short_date;
          const std::wstring& datetime = time + L"\n" + date;
          rt->DrawText(datetime.c_str(), static_cast<UINT32>(datetime.length()), app->text_format, layout_rect, white_brush);

          rt->EndDraw();

          ValidateRect(window, nullptr);
        }
        return 0;
      }
    }
  }

  return DefWindowProcW(window, message, wparam, lparam);
}

HWND create_clock_window(HINSTANCE instance) {
  static bool registered = false;
  if (!registered) {
    registered = true;

    WNDCLASSW wc = { };
    wc.lpfnWndProc = window_callback;
    wc.hInstance = instance;
    wc.lpszClassName = L"win11-clock-classname";
    RegisterClassW(&wc);
  }

  constexpr DWORD window_style = WS_POPUP;
  constexpr DWORD extended_window_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED;

  HWND window = CreateWindowExW(extended_window_style, L"win11-clock-classname", L"", window_style, 0, 0, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, nullptr);
  SetLayeredWindowAttributes(window, RGB(0, 0, 0), 0, LWA_COLORKEY);

  return window;
}

bool add_notification_area_icon(HWND window) {
  NOTIFYICONDATAW data = { };
  data.cbSize = sizeof(data);
  data.hWnd = window;
  data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  data.uCallbackMessage = WM_CLOCK_NOTIFY_COMMAND;
  data.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
  wcscpy_s(data.szTip, std::size(data.szTip), L"win11-clock");

  return Shell_NotifyIconW(NIM_ADD, &data) == TRUE;
}

void remove_notification_area_icon(HWND window) {
  NOTIFYICONDATAW data = { };
  data.cbSize = sizeof(data);
  data.hWnd = window;
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
        constexpr UINT kCmdQuit = 255;

        if (lparam == WM_RBUTTONUP) {
          auto checked = [](bool is) -> UINT { return is ? static_cast<UINT>(MF_CHECKED) : static_cast<UINT>(MF_UNCHECKED); };

          HMENU menu = CreatePopupMenu();

          HMENU position_menu = CreatePopupMenu();
          AppendMenuW(position_menu, checked(app->settings.position == Position::BottomLeft), kCmdPositionBottomLeft, L"Bottom Left");
          AppendMenuW(position_menu, checked(app->settings.position == Position::BottomRight), kCmdPositionBottomRight, L"Bottom Right");
          AppendMenuW(position_menu, checked(app->settings.position == Position::TopLeft), kCmdPositionTopLeft, L"Top Left");
          AppendMenuW(position_menu, checked(app->settings.position == Position::TopRight), kCmdPositionTopRight, L"Top Right");
          AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(position_menu), L"Position");

          HMENU date_menu = CreatePopupMenu();
          AppendMenuW(date_menu, checked(!app->settings.long_date), kCmdFormatShortDate, app->short_date.c_str());
          AppendMenuW(date_menu, checked(app->settings.long_date), kCmdFormatLongDate, app->long_date.c_str());
          AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(date_menu), L"Date Format");

          HMENU time_menu = CreatePopupMenu();
          AppendMenuW(time_menu, checked(!app->settings.long_time), kCmdFormatShortTime, app->short_time.c_str());
          AppendMenuW(time_menu, checked(app->settings.long_time), kCmdFormatLongTime, app->long_time.c_str());
          AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(time_menu), L"Time Format");

          AppendMenuW(menu, checked(app->settings.on_primary_display), kCmdPrimaryDisplay, L"On Primary Display");
          AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
          AppendMenuW(menu, MF_STRING, kCmdQuit, L"Exit");

          SetForegroundWindow(window);

          POINT mouse;
          GetCursorPos(&mouse);
          UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, mouse.x, mouse.y, 0, window, nullptr));

          DestroyMenu(menu);
          DestroyMenu(position_menu);
          DestroyMenu(time_menu);
          DestroyMenu(date_menu);

          switch (cmd) {
            case kCmdQuit: DestroyWindow(window); break;
            case kCmdPrimaryDisplay: app->settings.on_primary_display = !app->settings.on_primary_display; break;
            case kCmdPositionBottomLeft: app->settings.position = Position::BottomLeft; break;
            case kCmdPositionBottomRight: app->settings.position = Position::BottomRight; break;
            case kCmdPositionTopLeft: app->settings.position = Position::TopLeft; break;
            case kCmdPositionTopRight: app->settings.position = Position::TopRight; break;
            case kCmdFormatLongDate:  app->settings.long_date = true; break;
            case kCmdFormatShortDate: app->settings.long_date = false; break;
            case kCmdFormatLongTime: app->settings.long_time = true; break;
            case kCmdFormatShortTime: app->settings.long_time = false; break;
          }

          settings_changed(app); // @TODO: unnecessary
        }
        return 0;
      }

      case WM_DISPLAYCHANGE: OutputDebugStringA("WM_DISPLAYCHANGE\n"); break;
      case WM_DPICHANGED: OutputDebugStringA("WM_DPICHANGED\n"); break;
      case WM_INPUTLANGCHANGE: OutputDebugStringA("WM_INPUTLANGCHANGE\n"); break;
      case WM_DEVICECHANGE: OutputDebugStringA("WM_DEVICECHANGE\n"); break;
      case WM_TIMECHANGE: OutputDebugStringA("WM_TIMECHANGE\n"); break;

      case WM_TIMER: {
        SYSTEMTIME time;
        GetLocalTime(&time);
        app->short_date = utils::format_date(time, app->format.locale, app->format.short_date);
        app->short_time = utils::format_time(time, app->format.locale, app->format.short_time);
        app->long_date = utils::format_date(time, app->format.locale, app->format.long_date);
        app->long_time = utils::format_time(time, app->format.locale, app->format.long_time);

        for (HWND hwnd : app->clock_windows) {
          InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
      }
    }
  }

  return DefWindowProcW(window, message, wparam, lparam);
}

HWND create_dummy_window(HINSTANCE instance) {
  WNDCLASSW wc = { };
  wc.lpfnWndProc = dummy_window_callback;
  wc.hInstance = instance;
  wc.lpszClassName = L"win11-clock-dummy-classname";
  RegisterClassW(&wc);

  return CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
}

DeviceDependentResources create_device_dependent_resources(HWND window, ID2D1Factory* factory) {
  const Int2 size = utils::window_client_size(window);
  const uint32_t width = static_cast<uint32_t>(size.x);
  const uint32_t height = static_cast<uint32_t>(size.y);

  DeviceDependentResources result;
  factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(width, height)), &result.rt);
  result.rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &result.white_brush);
  result.rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), &result.red_brush);
  result.rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Green), &result.green_brush);
  return result;
}

// @TODO:
App app;

void CALLBACK win_event_hook(HWINEVENTHOOK hook, DWORD event, HWND window, LONG id_object, LONG id_child, DWORD id_event_thread, DWORD event_time) {
  for (HWND hwnd : app.clock_windows) {
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

int CALLBACK wWinMain(HINSTANCE instance, HINSTANCE ignored, PWSTR command_line, int show_command) {
  // @NOTE: Named mutex is used to prevent multiple instances of
  // this program running at once.
  const wchar_t* guid = L"6b54d0d4-ac9f-4ce7-b1b4-daa3527c935e";
  HANDLE mutex = CreateMutexW(nullptr, true, guid);
  if (GetLastError() != ERROR_SUCCESS) return 0;

  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

  const std::wstring temp_directory = utils::get_temp_directory();
  const std::wstring settings_absolute_path = temp_directory + L"settings.dat";
  SHCreateDirectoryExW(nullptr, temp_directory.c_str(), nullptr);
  app.settings = utils::load_settings(settings_absolute_path);

  // @TODO: handle no locale found
  app.format.locale = utils::get_user_default_locale_name();

  // @TODO: handle no format found
  app.format.short_date = utils::get_date_format(app.format.locale, DATE_SHORTDATE);
  app.format.long_date = utils::get_date_format(app.format.locale, DATE_LONGDATE);
  app.format.short_time = utils::get_time_format(app.format.locale, TIME_NOSECONDS);
  app.format.long_time = utils::get_time_format(app.format.locale, 0);

  SYSTEMTIME time;
  GetLocalTime(&time);
  app.short_date = utils::format_date(time, app.format.locale, app.format.short_date);
  app.short_time = utils::format_time(time, app.format.locale, app.format.short_time);
  app.long_date = utils::format_date(time, app.format.locale, app.format.long_date);
  app.long_time = utils::format_time(time, app.format.locale, app.format.long_time);

  // @NOTE: Dummy window is used to have one window always present
  // even in the case when there are no "clock" windows.
  app.dummy_window = create_dummy_window(instance);
  if (app.dummy_window) {
    SetWindowLongPtrW(app.dummy_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

    // @TODO: error checking
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &app.d2d_factory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&app.dwrite_factory));
    app.dwrite_factory->CreateTextFormat(L"Segoe UI Variable", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, app.format.locale.c_str(), &app.text_format);
    app.text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    app.text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    app.monitors = utils::get_display_monitors();

    const size_t monitor_count = app.monitors.size();
    for (size_t i = 0; i < monitor_count; ++i) {
      const Monitor& monitor = app.monitors[i];
      HWND window = create_clock_window(instance);
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));

      const Int2 size = compute_clock_window_size(monitor.dpi);
      const Int2 position = compute_clock_window_position(size, monitor.position, monitor.size, app.settings.position);

      SetWindowPos(window, HWND_TOPMOST, position.x, position.y, size.x, size.y, SWP_NOACTIVATE);
      ShowWindow(window, SW_SHOW);

      app.clock_windows.push_back(window);

      app.device_dependent_resources.push_back(create_device_dependent_resources(window, app.d2d_factory));
    }

    const UINT_PTR timer = SetTimer(app.dummy_window, 0, 1000, nullptr);

    // @NOTE: How is the topmost if more than one wants to be?
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, win_event_hook, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    MSG msg = { };
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    // @NOTE: windows will do the clean up anyways...
    utils::save_settings(app.settings, settings_absolute_path);

    KillTimer(app.dummy_window, timer);
    UnhookWinEvent(hook);
  }

  ReleaseMutex(mutex);

  return 0;
}
