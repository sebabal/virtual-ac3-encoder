// ComUtil.h — small COM/Win32 helpers shared by the WASAPI code.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>

template <class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// RAII CoInitializeEx (multithreaded apartment).
struct ComApartment
{
  HRESULT hr;
  ComApartment() { hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
  ~ComApartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
  bool ok() const { return SUCCEEDED(hr); }
};

std::string  Narrow(const wchar_t* w);
std::wstring Widen(const char* s);
std::string  HrStr(HRESULT hr); // "0x........"

// Log and return false on failure.
#define HR_FAIL(call, what)                                                              \
  do {                                                                                   \
    HRESULT _hr = (call);                                                                \
    if (FAILED(_hr)) {                                                                   \
      std::fprintf(stderr, "[%s] %s failed: %s\n", __func__, (what), HrStr(_hr).c_str());\
      return false;                                                                      \
    }                                                                                    \
  } while (0)
