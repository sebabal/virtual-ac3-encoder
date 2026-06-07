// DeviceEnum.cpp — see DeviceEnum.h.
#include "DeviceEnum.h"

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>

#include <algorithm>
#include <cwctype>

namespace {

// Read one endpoint's properties into EndpointInfo.
EndpointInfo Describe(IMMDevice* dev)
{
  EndpointInfo info;

  LPWSTR id = nullptr;
  if (SUCCEEDED(dev->GetId(&id)) && id)
  {
    info.id = id;
    CoTaskMemFree(id);
  }

  ComPtr<IPropertyStore> props;
  if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)))
  {
    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
      info.name = v.pwszVal;
    PropVariantClear(&v);

    PropVariantInit(&v);
    if (SUCCEEDED(props->GetValue(PKEY_AudioEndpoint_FormFactor, &v)) && v.vt == VT_UI4)
      info.isSpdif = (v.ulVal == SPDIF);
    PropVariantClear(&v);

    PropVariantInit(&v);
    if (SUCCEEDED(props->GetValue(PKEY_AudioEngine_DeviceFormat, &v)) && v.vt == VT_BLOB &&
        v.blob.cbSize >= sizeof(WAVEFORMATEX))
    {
      auto* wf = reinterpret_cast<const WAVEFORMATEX*>(v.blob.pBlobData);
      info.mixChannels = wf->nChannels;
      info.mixRate = wf->nSamplesPerSec;
    }
    PropVariantClear(&v);
  }
  return info;
}

bool ContainsCI(const std::wstring& hay, const std::wstring& needle)
{
  auto lower = [](std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
  };
  return lower(hay).find(lower(needle)) != std::wstring::npos;
}

} // namespace

namespace DeviceEnum {

std::vector<EndpointInfo> List(EDataFlow flow)
{
  std::vector<EndpointInfo> out;
  ComPtr<IMMDeviceEnumerator> en;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&en))))
    return out;

  ComPtr<IMMDeviceCollection> coll;
  if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)))
    return out;

  UINT count = 0;
  coll->GetCount(&count);
  for (UINT i = 0; i < count; ++i)
  {
    ComPtr<IMMDevice> dev;
    if (SUCCEEDED(coll->Item(i, &dev)))
      out.push_back(Describe(dev.Get()));
  }
  return out;
}

bool GetById(const std::wstring& id, ComPtr<IMMDevice>& out)
{
  ComPtr<IMMDeviceEnumerator> en;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&en))))
    return false;
  return SUCCEEDED(en->GetDevice(id.c_str(), &out));
}

bool FindByNameSubstring(EDataFlow flow, const std::wstring& sub, ComPtr<IMMDevice>& out,
                         EndpointInfo& info)
{
  ComPtr<IMMDeviceEnumerator> en;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&en))))
    return false;

  ComPtr<IMMDeviceCollection> coll;
  if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)))
    return false;

  UINT count = 0;
  coll->GetCount(&count);
  for (UINT i = 0; i < count; ++i)
  {
    ComPtr<IMMDevice> dev;
    if (FAILED(coll->Item(i, &dev)))
      continue;
    EndpointInfo ei = Describe(dev.Get());
    if (ContainsCI(ei.name, sub))
    {
      out = dev;
      info = ei;
      return true;
    }
  }
  return false;
}

bool GetDefault(EDataFlow flow, ERole role, ComPtr<IMMDevice>& out, EndpointInfo& info)
{
  ComPtr<IMMDeviceEnumerator> en;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&en))))
    return false;
  if (FAILED(en->GetDefaultAudioEndpoint(flow, role, &out)))
    return false;
  info = Describe(out.Get());
  return true;
}

void Print(const std::vector<EndpointInfo>& eps)
{
  for (size_t i = 0; i < eps.size(); ++i)
  {
    const auto& e = eps[i];
    std::printf("  [%zu] %-45s %s  (%uch/%uHz)\n  %s      id=%s\n", i, Narrow(e.name.c_str()).c_str(),
                e.isSpdif ? "[SPDIF]" : "       ", e.mixChannels, e.mixRate, "",
                Narrow(e.id.c_str()).c_str());
  }
}

} // namespace DeviceEnum
