#pragma once
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propsys.h>

// {870AF99C-171D-4F9E-AF0D-E63DF40C2BC9}
static const CLSID CLSID_PolicyConfigClient = {0x870AF99C, 0x171D, 0x4F9E, {0xAF, 0x0D, 0xE6, 0x3D, 0xF4, 0x0C, 0x2B, 0xC9}};

// Undocumented. Stable since Vista. Vtable verified on Win10/Win11 through 24H2.
// All slots must be declared to keep the compiler vtable layout correct.
// Only SetDefaultEndpoint (slot 13) is actually called.
MIDL_INTERFACE("F8679F50-850A-41CF-9C72-430F290290C8")
IPolicyConfig : public IUnknown {
  public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, BOOL, WAVEFORMATEX**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, BOOL, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, BOOL, PROPERTYKEY const&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, BOOL, PROPERTYKEY const&, PROPVARIANT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, BOOL) = 0;
};
