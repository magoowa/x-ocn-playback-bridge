#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <thumbcache.h>
#include <dshow.h>
#include <shlwapi.h>
#include <atomic>
#include <new>
#include <string>
#include "decode.h"

const CLSID CLSID_Thumbnail{0xb94a5d91,0x3be4,0x4c81,{0x99,0x34,0x71,0xc6,0x4c,0xd0,0x36,0x77}};
const CLSID CLSID_Icaros{0xc5aec3ec,0xe812,0x4677,{0xa9,0xa7,0x4f,0xee,0x1f,0x9a,0xa0,0x00}};
std::atomic<long> objects{0};

class Thumbnail final : public IInitializeWithFile, public IInitializeWithStream, public IThumbnailProvider {
    std::atomic<ULONG> refs{1};
    std::wstring path;
    IStream* input = nullptr;
public:
    Thumbnail() { ++objects; }
    ~Thumbnail() { if (input) input->Release(); --objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (id == IID_IUnknown || id == IID_IInitializeWithFile)
            *result = static_cast<IInitializeWithFile*>(this);
        else if (id == IID_IInitializeWithStream)
            *result = static_cast<IInitializeWithStream*>(this);
        else if (id == IID_IThumbnailProvider)
            *result = static_cast<IThumbnailProvider*>(this);
        if (!*result) return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --refs;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE Initialize(LPCWSTR file, DWORD) override {
        if (!file) return E_POINTER;
        if (!path.empty()) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        try { path = file; } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* stream, DWORD) override {
        if (!stream) return E_POINTER;
        if (input || !path.empty()) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        input = stream;
        input->AddRef();
        return S_OK;
    }
    HRESULT Fallback(UINT size, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) {
        IUnknown* unknown = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_Icaros, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&unknown));
        if (FAILED(hr)) return hr;
        IThumbnailProvider* provider = nullptr;
        IInitializeWithFile* withFile = nullptr;
        IInitializeWithStream* withStream = nullptr;
        hr = unknown->QueryInterface(IID_PPV_ARGS(&provider));
        if (SUCCEEDED(hr)) {
            hr = input ? E_NOINTERFACE : unknown->QueryInterface(IID_PPV_ARGS(&withFile));
            if (SUCCEEDED(hr) && withFile) {
                hr = withFile->Initialize(path.c_str(), STGM_READ);
            } else if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&withStream)))) {
                IStream* stream = nullptr;
                if (input) {
                    LARGE_INTEGER zero{};
                    hr = input->Seek(zero, STREAM_SEEK_SET, nullptr);
                    if (SUCCEEDED(hr)) { stream = input; stream->AddRef(); }
                } else {
                    hr = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream);
                }
                if (SUCCEEDED(hr)) { hr = withStream->Initialize(stream, STGM_READ); stream->Release(); }
            }
            if (SUCCEEDED(hr)) hr = provider->GetThumbnail(size, bitmap, alpha);
        }
        if (withStream) withStream->Release();
        if (withFile) withFile->Release();
        if (provider) provider->Release();
        unknown->Release();
        return hr;
    }
    bool ReadStream(Clip& clip, Bytes& frame) {
        STATSTG stat{};
        if (FAILED(input->Stat(&stat, STATFLAG_NONAME))) return false;
        uint64_t pos = 0;
        while (pos < stat.cbSize.QuadPart && pos <= (64ull << 20)) {
            LARGE_INTEGER seek{};
            seek.QuadPart = pos;
            if (FAILED(input->Seek(seek, STREAM_SEEK_SET, nullptr))) return false;
            uint8_t key[16], first;
            ULONG got = 0;
            if (FAILED(input->Read(key, 16, &got)) || got != 16 || BE(key, 4) != 0x060e2b34 ||
                FAILED(input->Read(&first, 1, &got)) || got != 1 || first == 128 || first > 136) return false;
            uint64_t length = first;
            unsigned lengthBytes = 1;
            if (first > 128) {
                const unsigned count = first - 128;
                uint8_t bytes[8]{};
                if (FAILED(input->Read(bytes, count, &got)) || got != count) return false;
                length = 0;
                for (unsigned i = 0; i < count; ++i) length = (length << 8) | bytes[i];
                lengthBytes += count;
            }
            const uint64_t value = pos + 16 + lengthBytes;
            if (value > stat.cbSize.QuadPart || length > stat.cbSize.QuadPart - value) return false;
            const uint32_t type = BE(key + 4, 4), label = BE(key + 8, 4);
            if (type == 0x01020106 && (label >> 16) == 0x0e06) {
                if (length < 64 || length > (64u << 20)) return false;
                frame.resize(static_cast<size_t>(length));
                return SUCCEEDED(input->Read(frame.data(), static_cast<ULONG>(length), &got)) && got == length;
            }
            if (type == 0x02530101 && label == 0x0d010101 && length <= 65536 &&
                (key[14] == 0x27 || key[14] == 0x28 || key[14] == 0x29) && !clip.fpsNum) {
                Bytes descriptor(static_cast<size_t>(length));
                if (FAILED(input->Read(descriptor.data(), static_cast<ULONG>(length), &got)) || got != length) return false;
                clip.Descriptor(descriptor, true);
            }
            pos = value + length;
        }
        return false;
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT size, HBITMAP* bitmap, WTS_ALPHATYPE* alpha) override {
        if (!bitmap || !alpha) return E_POINTER;
        *bitmap = nullptr;
        *alpha = WTSAT_RGB;
        if ((!input && path.empty()) || !size || size > 4096) return E_INVALIDARG;
        try {
            Clip clip;
            Bytes frame;
            Decoder decoder;
            if (input) {
                if (!ReadStream(clip, frame)) return Fallback(size, bitmap, alpha);
            } else {
                auto file = Open(path.c_str());
                if (!file || !clip.Load(file.get()) || !Read(file.get(), clip.video[clip.video.size()/5], frame))
                    return Fallback(size, bitmap, alpha);
            }
            if (FAILED(decoder.Init(frame, clip))) return Fallback(size, bitmap, alpha);
            Bytes rgb(decoder.Size());
            decoder.Render(rgb.data());
            const double ratio = clip.aspectX && clip.aspectY
                ? double(clip.aspectX) / clip.aspectY : double(decoder.width) / decoder.height;
            const unsigned outW = ratio >= 1 ? size : std::max(1u, unsigned(size * ratio + .5));
            const unsigned outH = ratio >= 1 ? std::max(1u, unsigned(size / ratio + .5)) : size;
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = outW;
            info.bmiHeader.biHeight = -static_cast<LONG>(outH);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            void* bits = nullptr;
            HBITMAP result = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!result) return E_OUTOFMEMORY;
            auto* output = static_cast<uint8_t*>(bits);
            for (unsigned y = 0; y < outH; ++y) {
                const auto* row = rgb.data() + size_t(decoder.height - 1 - uint64_t(y) * decoder.height / outH) * decoder.Stride();
                for (unsigned x = 0; x < outW; ++x) {
                    const auto* source = row + 3 * (uint64_t(x) * decoder.width / outW);
                    *output++ = source[0]; *output++ = source[1]; *output++ = source[2]; *output++ = 255;
                }
            }
            *bitmap = result;
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    }
};

class Factory final : public IClassFactory {
    std::atomic<ULONG> refs{1};
public:
    Factory() { ++objects; }
    ~Factory() { --objects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IClassFactory) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { ULONG n=--refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID id, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* instance = new (std::nothrow) Thumbnail();
        if (!instance) return E_OUTOFMEMORY;
        HRESULT hr=instance->QueryInterface(id,out);
        instance->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { if(lock) ++objects; else --objects; return S_OK; }
};
STDAPI DllGetClassObject(REFCLSID clsid, REFIID id, void** out) {
    if (clsid != CLSID_Thumbnail) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory=new (std::nothrow) Factory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr=factory->QueryInterface(id,out);
    factory->Release();
    return hr;
}
STDAPI DllCanUnloadNow() {
    return objects == 0 ? S_OK : S_FALSE;
}

