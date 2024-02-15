#pragma once
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <seEngine.h>
#include <Windows.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <shellapi.h>
#include <d3d12.h>
#include <d3dx12.h>
#include <d3dx12_barriers.h>
#include <d3dx12_check_feature_support.h>
#include <d3dx12_resource_helpers.h>

#define USE_PIX
#include <pix3.h>

using Microsoft::WRL::ComPtr;

#define CHECKHR(x) { HRESULT ___hr = x; seAssert(SUCCEEDED(___hr), "HRESULT Failure!"); }

// Assign a name to the object to aid with debugging.
#if defined(_DEBUG) || defined(DBG)
inline void SetName(ID3D12Object* pObject, LPCWSTR name)
{
    pObject->SetName(name);
}
inline void SetNameIndexed(ID3D12Object* pObject, LPCWSTR name, UINT index)
{
    WCHAR fullName[50];
    if (swprintf_s(fullName, L"%s[%u]", name, index) > 0)
    {
        pObject->SetName(fullName);
    }
}
#else
inline void SetName(ID3D12Object*, LPCWSTR)
{
}
inline void SetNameIndexed(ID3D12Object*, LPCWSTR, UINT)
{
}
#endif

// Naming helper for ComPtr<T>.
// Assigns the name of the variable as the name of the object.
// The indexed variant will include the index in the name of the object.
#define NAME_D3D12_OBJECT(x) SetName((x).Get(), L#x)
#define NAME_D3D12_OBJECT_INDEXED(x, n) SetNameIndexed((x)[n].Get(), L#x, n)