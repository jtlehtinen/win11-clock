#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")

#define NOMINMAX
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>

constexpr UINT WM_CLOCK_NOTIFY_COMMAND = (WM_USER + 1);

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

std::wstring format_current_datetime(const std::wstring& locale, const std::wstring& date_format, const std::wstring& time_format) {
  SYSTEMTIME time;
  GetLocalTime(&time);
  return format_time(time, locale, time_format) + L"\n" + format_date(time, locale, date_format);
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

void remove_notification_are_icon(HWND window) {
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

  switch (message) {
    case WM_DESTROY: {
      remove_notification_are_icon(window);
      PostQuitMessage(0);
      return 0;
    }

    case WM_CLOCK_NOTIFY_COMMAND: {
      constexpr UINT kCmdQuit = 255;

      if (lparam == WM_RBUTTONUP) {
        HMENU menu = CreatePopupMenu();

        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, kCmdQuit, L"Exit");

        POINT mouse;
        GetCursorPos(&mouse);
        UINT cmd = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, mouse.x, mouse.y, 0, window, nullptr));
        if (cmd == kCmdQuit) PostQuitMessage(0);
      }
      return 0;
    }

    case WM_PAINT:
      ValidateRect(window, nullptr);
      return 0;
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

int CALLBACK wWinMain(HINSTANCE instance, HINSTANCE ignored, PWSTR command_line, int show_command) {
  UNREFERENCED_PARAMETER(ignored);
  UNREFERENCED_PARAMETER(command_line);
  UNREFERENCED_PARAMETER(show_command);

  // @TODO: handle no locale found
  std::wstring locale = get_user_default_locale_name();

  // @TODO: handle no format found
  std::wstring short_date = get_date_format(locale, DATE_SHORTDATE);
  std::wstring long_date = get_date_format(locale, DATE_LONGDATE);
  std::wstring short_time = get_time_format(locale, TIME_NOSECONDS);
  std::wstring long_time = get_time_format(locale, 0);

  std::wstring short_datetime = format_current_datetime(locale, short_date, short_time);
  std::wstring long_datetime = format_current_datetime(locale, long_date, long_time);

  OutputDebugStringW(short_datetime.c_str());
  OutputDebugStringW(L"\n");
  OutputDebugStringW(long_datetime.c_str());
  OutputDebugStringW(L"\n");

  // @NOTE: Dummy window is used to have one window always present
  // even in the case when there are no "clock" windows.
  HWND dummy_window = create_dummy_window(instance);
  if (!dummy_window) return 1;

  MSG msg = { };
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  return 0;
}
