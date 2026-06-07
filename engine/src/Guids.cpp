// Guids.cpp
//
// Single translation unit that *defines* (rather than merely declares) all the COM GUIDs,
// property keys and KS media subformats we use. Every other file includes the same headers
// without INITGUID and links against the definitions emitted here.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <initguid.h>                       // turns the DEFINE_GUID/DEFINE_PROPERTYKEY below into definitions
#include <mmdeviceapi.h>                    // CLSID_MMDeviceEnumerator, IID_IMMDeviceEnumerator
#include <audioclient.h>                    // IID_IAudioClient, IID_IAudioRenderClient, ...
#include <functiondiscoverykeys_devpkey.h>  // PKEY_Device_FriendlyName, PKEY_AudioEndpoint_FormFactor, ...
#include <ks.h>
#include <ksmedia.h>                        // KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL, KSDATAFORMAT_SUBTYPE_PCM
