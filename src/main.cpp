#define NOMINMAX
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

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

int CALLBACK wWinMain(HINSTANCE instance, HINSTANCE ignored, PWSTR command_line, int show_command) {
  UNREFERENCED_PARAMETER(instance);
  UNREFERENCED_PARAMETER(ignored);
  UNREFERENCED_PARAMETER(command_line);
  UNREFERENCED_PARAMETER(show_command);

  // @TODO: what to do if no locale found? can't use default either,
  // since there is no guarantee that exists within the system?
  std::wstring locale = get_user_default_locale_name(); // default: L"en-FI"?

  // @TODO: what to do if no formats found? can't use default either,
  // since there is no guarantee that exists within the system?
  std::wstring short_date = get_date_format(locale, DATE_SHORTDATE); // default: L"dd/MM/yyyy"?
  std::wstring long_date = get_date_format(locale, DATE_LONGDATE); // default: L"dddd, d MMMM yyyy"?
  std::wstring short_time = get_time_format(locale, TIME_NOSECONDS); // default: L"H.mm"?
  std::wstring long_time = get_time_format(locale, 0); // default: L"H.mm.ss"?

  std::wstring short_datetime = format_current_datetime(locale, short_date, short_time);
  std::wstring long_datetime = format_current_datetime(locale, long_date, long_time);

  OutputDebugStringW(short_datetime.c_str());
  OutputDebugStringW(L"\n");
  OutputDebugStringW(long_datetime.c_str());
  OutputDebugStringW(L"\n");

  return 0;
}
