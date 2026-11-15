#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <d3d10.h>
#include <wrl/client.h>
#include <memory>
#include "gpu_render.h"


interface __declspec(uuid("2BB66002-46B7-4F13-9036-7053328742BE")) ID3D11DecoderConfiguration : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE ActivateD3D11Decoding(ID3D11Device*, ID3D11DeviceContext*, HANDLE, UINT) = 0;
    virtual UINT STDMETHODCALLTYPE GetD3D11AdapterIndex() = 0;
};
interface __declspec(uuid("BC8753F5-0AC8-4806-8E5F-A12B2AFE153E")) IMediaSampleD3D11 : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetD3D11Texture(int, ID3D11Texture2D**, UINT*) = 0;
};

struct GpuContext {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    GpuRender renderer;
    HANDLE mutex = nullptr;
    ~GpuContext() { renderer.Reset(); if (mutex) CloseHandle(mutex); }
    HRESULT Connect(IPin* pin) {
        Microsoft::WRL::ComPtr<ID3D11DecoderConfiguration> config;
        HRESULT hr = pin->QueryInterface(IID_PPV_ARGS(&config));
        if (FAILED(hr)) return hr;
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return hr;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(config->GetD3D11AdapterIndex(), &adapter);
        if (FAILED(hr)) return hr;
        D3D_FEATURE_LEVEL level{};
        hr = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context);
        if (FAILED(hr)) return hr;
        Microsoft::WRL::ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(context.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
        mutex = CreateMutexW(nullptr, FALSE, nullptr);
        if (!mutex) return HRESULT_FROM_WIN32(GetLastError());
        return config->ActivateD3D11Decoding(device.Get(), context.Get(), mutex, 0);
    }
};

class GpuSample final : public CMediaSample, public IMediaSampleD3D11 {
    std::shared_ptr<GpuContext> gpu;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
public:
    GpuSample(CBaseAllocator* allocator, std::shared_ptr<GpuContext> owner, ID3D11Texture2D* value, HRESULT* hr)
        : CMediaSample(NAME("X-OCN GPU sample"), allocator, hr, nullptr, 0), gpu(std::move(owner)), texture(value) {}
    STDMETHODIMP QueryInterface(REFIID id, void** out) override {
        if (id == __uuidof(IMediaSampleD3D11)) return GetInterface(static_cast<IMediaSampleD3D11*>(this), out);
        return CMediaSample::QueryInterface(id, out);
    }
    STDMETHODIMP_(ULONG) AddRef() override { return CMediaSample::AddRef(); }
    STDMETHODIMP_(ULONG) Release() override { return CMediaSample::Release(); }
    STDMETHODIMP GetPointer(BYTE** output) override { if (output) *output = nullptr; return E_NOTIMPL; }
    STDMETHODIMP GetD3D11Texture(int view, ID3D11Texture2D** output, UINT* slice) override {
        if (!output || !slice) return E_POINTER;
        *output = nullptr;
        if (view != 0) return E_INVALIDARG;
        *output = texture.Get(); (*output)->AddRef(); *slice = 0;
        return S_OK;
    }
};

class GpuAllocator final : public CBaseAllocator {
    std::shared_ptr<GpuContext> gpu;
    unsigned width, height;
public:
    GpuAllocator(std::shared_ptr<GpuContext> value, unsigned w, unsigned h, HRESULT* hr)
        : CBaseAllocator(NAME("X-OCN GPU allocator"), nullptr, hr), gpu(std::move(value)), width(w), height(h) {}
protected:
    void Free() override {
        CMediaSample* sample = nullptr;
        while ((sample = m_lFree.RemoveHead())) delete sample;
        m_lAllocated = 0;
    }
    HRESULT Alloc() override {
        CAutoLock lock(this);
        HRESULT hr = CBaseAllocator::Alloc();
        if (FAILED(hr)) return hr;
        Free();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
        for (LONG i=0; i<m_lCount; ++i) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            hr = gpu->device->CreateTexture2D(&desc, nullptr, &texture);
            if (FAILED(hr)) { Free(); return hr; }
            auto* sample = new (std::nothrow) GpuSample(this, gpu, texture.Get(), &hr);
            if (!sample || FAILED(hr)) { delete sample; Free(); return sample ? hr : E_OUTOFMEMORY; }
            m_lFree.Add(sample);
        }
        m_lAllocated=m_lCount; m_bChanged=FALSE;
        return S_OK;
    }
};
