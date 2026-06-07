// ComUtil.cpp
#include "ComUtil.h"

std::string Narrow(const wchar_t* w)
{
  if (!w) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Widen(const char* s)
{
  if (!s) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
  return w;
}

std::string HrStr(HRESULT hr)
{
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%08lx", static_cast<unsigned long>(hr));
  return buf;
}
