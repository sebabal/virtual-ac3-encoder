// DeviceEnum.h — enumerate WASAPI render/capture endpoints and locate ours.
#pragma once

#include "ComUtil.h"
#include <mmdeviceapi.h>

#include <string>
#include <vector>

struct EndpointInfo
{
  std::wstring id;          // immutable endpoint id (persist this in config)
  std::wstring name;        // friendly name
  bool         isSpdif = false;  // PKEY_AudioEndpoint_FormFactor == SPDIF
  unsigned     mixChannels = 0;  // from PKEY_AudioEngine_DeviceFormat (best effort)
  unsigned     mixRate = 0;
};

namespace DeviceEnum
{
// flow = eRender (outputs) or eCapture (recording).
std::vector<EndpointInfo> List(EDataFlow flow);

// Resolve an IMMDevice by its endpoint id.
bool GetById(const std::wstring& id, ComPtr<IMMDevice>& out);

// Find the first endpoint whose friendly name contains `sub` (case-insensitive).
bool FindByNameSubstring(EDataFlow flow, const std::wstring& sub,
                         ComPtr<IMMDevice>& out, EndpointInfo& info);

// Default endpoint for a flow/role.
bool GetDefault(EDataFlow flow, ERole role, ComPtr<IMMDevice>& out, EndpointInfo& info);

void Print(const std::vector<EndpointInfo>& eps);
} // namespace DeviceEnum
