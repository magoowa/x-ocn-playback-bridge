#define NOMINMAX
#include <streams.h>
#include <ks.h>
#include <ksmedia.h>
#include <dvdmedia.h>
#include <new>
#include <numeric>
#include <olectl.h>
#include <string>

#include "decode.h"

constexpr CLSID FilterId{0x927eb97c, 0xda96, 0x4af7, {0x9d,0x70,0xf3,0x14,0x32,0x67,0x48,0x62}};
constexpr CLSID PageId{0x63e54d1a,0x9352,0x42bf,{0xa3,0xbb,0x80,0xa6,0x94,0x87,0x38,0x12}};
struct __declspec(uuid("D482AB74-5C97-4D6B-9CBB-22A8E9414930")) IXocnSettings : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInfo(unsigned*, unsigned*, unsigned*, unsigned*, unsigned*, unsigned*, unsigned*, unsigned*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSqueeze(unsigned) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMetadata(BSTR*, unsigned*, unsigned*, unsigned*, BOOL*, ULONGLONG*, LONGLONG*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDisplayLut(BOOL) = 0;
};
struct __declspec(uuid("03481710-D73E-4674-839F-03EDE2D60ED8")) ISpecifyPropertyPages2 : ISpecifyPropertyPages {
    virtual HRESULT STDMETHODCALLTYPE CreatePage(const GUID&, IPropertyPage**) = 0;
};
HRESULT CreateSettingsPage(IPropertyPage** page);
constexpr wchar_t SettingsKey[] = L"Software\\XOCN-MPC";
constexpr wchar_t FilterName[] = L"Sony X-OCN MXF Source";

class Pin final : public CSourceStream, public CSourceSeeking {
    CCritSec seekLock;
    CCritSec restartLock;
    const Clip& clip;
    File file;
    Decoder* video;
    unsigned squeeze;
    bool half = false;
    unsigned OutputWidth() const { return video->width / (half ? 2 : 1); }
    unsigned OutputHeight() const { return video->height / (half ? 2 : 1); }
    unsigned OutputSize() const { return ((OutputWidth() * 3 + 3) & ~3u) * OutputHeight(); }
    void Render(BYTE* out) { if (half) video->RenderHalf(out); else video->Render(out); }
    Bytes packet;
    size_t frame = 0;
    PTP_WORK prefetchWork = nullptr;
    Bytes prefetchRGB;
    size_t prefetched = SIZE_MAX;
    HRESULT prefetchStatus = E_FAIL;
    bool pending = false;
    static void CALLBACK Prefetch(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) {
        auto* self = static_cast<Pin*>(context);
        try {
            self->prefetchStatus = E_FAIL;
            if (!Read(self->file.get(), self->clip.video[self->prefetched], self->packet)) return;
            self->prefetchStatus = self->video->Decode(self->packet);
            if (SUCCEEDED(self->prefetchStatus)) self->Render(self->prefetchRGB.data());
        } catch (const std::bad_alloc&) { self->prefetchStatus = E_OUTOFMEMORY; }
    }
    REFERENCE_TIME start = 0, stop = 0;
    double rate = 1;
    bool discontinuity = true;

    HRESULT Restart() {
        CAutoLock lock(&restartLock);
        if (!ThreadExists()) return S_OK;
        DeliverBeginFlush();
        Stop();
        DeliverEndFlush();
        return Run();
    }

    HRESULT ChangeStart() override { return Restart(); }
    HRESULT ChangeStop() override { return Restart(); }
    HRESULT ChangeRate() override { return Restart(); }

public:
    using CSourceStream::GetOwner;
    Pin(HRESULT* hr, CSource* source, const Clip& clip, File input, Decoder* decoder, unsigned squeezeValue)
        : CSourceStream(NAME("X-OCN stream"), hr, source, decoder ? L"Video" : L"Audio"),
          CSourceSeeking(NAME("X-OCN seeking"), static_cast<IPin*>(this), hr, &seekLock),
          clip(clip), file(std::move(input)), video(decoder), squeeze(squeezeValue) {
        m_rtDuration = m_rtStop = clip.Time(clip.video.size());
        wchar_t enabled[2]{};
        half = video && video->width >= 7680 && video->CanRenderHalf() &&
               GetEnvironmentVariableW(L"XOCN_MPV_PREVIEW_4K", enabled, 2) == 1 && enabled[0] == L'1';
        if (half) {
            try { prefetchRGB.resize(OutputSize()); prefetchWork = CreateThreadpoolWork(Prefetch, this, nullptr); }
            catch (const std::bad_alloc&) { prefetchRGB.clear(); }
        }
    }

    ~Pin() override {
        if (prefetchWork) { WaitForThreadpoolWorkCallbacks(prefetchWork, FALSE); CloseThreadpoolWork(prefetchWork); }
    }
    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID id, void** out) override {
        if (id == IID_IMediaSeeking) return GetInterface(static_cast<IMediaSeeking*>(this), out);
        return CSourceStream::NonDelegatingQueryInterface(id, out);
    }

    STDMETHODIMP SetRate(double requested) override {
        if (!std::isfinite(requested) || requested < 0.125 || requested > 8) return E_INVALIDARG;
        return CSourceSeeking::SetRate(requested);
    }

    HRESULT GetMediaType(CMediaType* mt) override {
        if (video) {
            mt->SetType(&MEDIATYPE_Video);
            mt->SetSubtype(&MEDIASUBTYPE_RGB24);
            mt->SetFormatType(&FORMAT_VideoInfo2);
            auto* info = reinterpret_cast<VIDEOINFOHEADER2*>(mt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER2)));
            if (!info) return E_OUTOFMEMORY;
            *info = {};
            info->AvgTimePerFrame = clip.Time(1);
            // MXF AspectRatio describes display proportions after anamorphic desqueeze.
            info->dwPictAspectRatioX = clip.aspectX ? clip.aspectX : video->width;
            info->dwPictAspectRatioY = clip.aspectY ? clip.aspectY : video->height;
            if (squeeze) {
                unsigned x = video->width * squeeze, y = video->height * 1000;
                const unsigned divisor = std::gcd(x, y);
                info->dwPictAspectRatioX = x / divisor;
                info->dwPictAspectRatioY = y / divisor;
            }
            info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info->bmiHeader.biWidth = OutputWidth();
            info->bmiHeader.biHeight = OutputHeight();
            info->bmiHeader.biPlanes = 1;
            info->bmiHeader.biBitCount = 24;
            info->bmiHeader.biSizeImage = OutputSize();
            mt->SetSampleSize(OutputSize());
        } else {
            const auto* subtype = clip.floatAudio ? &MEDIASUBTYPE_IEEE_FLOAT : &MEDIASUBTYPE_PCM;
            mt->SetType(&MEDIATYPE_Audio);
            mt->SetSubtype(subtype);
            mt->SetFormatType(&FORMAT_WaveFormatEx);
            auto* info = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mt->AllocFormatBuffer(sizeof(WAVEFORMATEXTENSIBLE)));
            if (!info) return E_OUTOFMEMORY;
            *info = {};
            info->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
            info->Format.nChannels = static_cast<WORD>(clip.channels);
            info->Format.nSamplesPerSec = clip.rate;
            info->Format.wBitsPerSample = static_cast<WORD>(clip.bits);
            info->Format.nBlockAlign = static_cast<WORD>(clip.channels * clip.bits / 8);
            info->Format.nAvgBytesPerSec = clip.rate * info->Format.nBlockAlign;
            info->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            info->Samples.wValidBitsPerSample = info->Format.wBitsPerSample;
            if (clip.channels == 1) info->dwChannelMask = KSAUDIO_SPEAKER_MONO;
            if (clip.channels == 2) info->dwChannelMask = KSAUDIO_SPEAKER_STEREO;
            if (clip.channels == 4) info->dwChannelMask = KSAUDIO_SPEAKER_QUAD;
            info->SubFormat = *subtype;
            mt->SetSampleSize(info->Format.nBlockAlign);
        }
        return S_OK;
    }

    HRESULT DecideBufferSize(IMemAllocator* allocator, ALLOCATOR_PROPERTIES* wanted) override {
        wanted->cBuffers = std::max(wanted->cBuffers, video ? 3L : 8L);
        wanted->cbBuffer = std::max(wanted->cbBuffer, LONG(video ? OutputSize() : clip.audioBytes));
        wanted->cbAlign = std::max(wanted->cbAlign, 1L);
        ALLOCATOR_PROPERTIES actual{};
        const HRESULT hr = allocator->SetProperties(wanted, &actual);
        if (FAILED(hr)) return hr;
        return actual.cbBuffer >= wanted->cbBuffer ? S_OK : E_FAIL;
    }

    HRESULT OnThreadStartPlay() override {
        {
            CAutoLock lock(&seekLock);
            start = std::clamp<REFERENCE_TIME>(m_rtStart, REFERENCE_TIME{0}, m_rtDuration);
            stop = std::clamp<REFERENCE_TIME>(m_rtStop, start, m_rtDuration);
            rate = m_dRateSeeking;
        }
        frame = clip.Frame(start);
        discontinuity = true;
        return DeliverNewSegment(start, stop, rate);
    }

    HRESULT FillBuffer(IMediaSample* sample) override {
        if (frame >= clip.video.size() || clip.Time(frame) >= stop) return S_FALSE;
        BYTE* output = nullptr;
        HRESULT hr = sample->GetPointer(&output);
        if (FAILED(hr)) return hr;
        const LONG capacity = sample->GetSize();
        try {
            if (video) {
                if (capacity < LONG(OutputSize())) return E_FAIL;
                if (pending) { WaitForThreadpoolWorkCallbacks(prefetchWork, FALSE); pending = false; }
                if (prefetchWork && prefetched == frame) {
                    if (FAILED(prefetchStatus)) return prefetchStatus;
                    memcpy(output, prefetchRGB.data(), OutputSize());
                } else {
                    if (!Read(file.get(), clip.video[frame], packet)) return E_FAIL;
                    hr = video->Decode(packet);
                    if (FAILED(hr)) return hr;
                    Render(output);
                }
                prefetched = SIZE_MAX;
                if (prefetchWork && frame + 1 < clip.video.size()) {
                    prefetched = frame + 1; pending = true; SubmitThreadpoolWork(prefetchWork);
                }
                sample->SetActualDataLength(OutputSize());
            } else {
                unsigned channel = 0;
                const unsigned stride = clip.bits / 8;
                size_t size = 0;
                const auto skipped = size_t((std::max(start, clip.Time(frame)) - clip.Time(frame)) *
                                            clip.rate / 10000000);
                const auto dropped = size_t((clip.Time(frame + 1) - std::min(stop, clip.Time(frame + 1))) *
                                            clip.rate / 10000000);
                for (const auto& track : clip.tracks) {
                    if (track.empty()) continue;
                    if (!Read(file.get(), track[frame], packet)) return E_FAIL;
                    const size_t samples = packet.size() / stride;
                    const size_t from = std::min(skipped, samples);
                    const size_t until = std::max(from, samples - std::min(dropped, samples));
                    size = (until - from) * stride * clip.channels;
                    if (size > size_t(capacity)) return E_FAIL;
                    for (size_t i = from; i < until; ++i)
                        memcpy(output + ((i - from) * clip.channels + channel) * stride,
                               &packet[i * stride], stride);
                    ++channel;
                }
                sample->SetActualDataLength(static_cast<LONG>(size));
            }
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
        REFERENCE_TIME from = REFERENCE_TIME((std::max(clip.Time(frame), start) - start) / rate);
        REFERENCE_TIME until = REFERENCE_TIME((std::min(clip.Time(frame + 1), stop) - start) / rate);
        sample->SetTime(&from, &until);
        sample->SetSyncPoint(TRUE);
        sample->SetDiscontinuity(discontinuity);
        discontinuity = false;
        ++frame;
        return S_OK;
    }
};

class Source final : public CSource, public IFileSourceFilter, public ISpecifyPropertyPages2, public IXocnSettings {
    std::wstring path;
    Clip clip;
    std::unique_ptr<Decoder> decoder;
    unsigned squeeze = 0;
    bool displayLut = false;

public:
    Source(LPUNKNOWN outer) : CSource(FilterName, outer, FilterId) {
        DWORD bytes = sizeof(squeeze);
        if (RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"Squeeze", RRF_RT_REG_DWORD, nullptr, &squeeze, &bytes) != ERROR_SUCCESS || squeeze > 3000 || (squeeze && squeeze < 500)) squeeze = 0;
        DWORD enabled = 0; bytes = sizeof(enabled);
        if (RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"DisplayLut", RRF_RT_REG_DWORD, nullptr, &enabled, &bytes) == ERROR_SUCCESS) displayLut = enabled == 1;
    }
    static CUnknown* WINAPI Create(LPUNKNOWN outer, HRESULT* result) {
        auto* source = new (std::nothrow) Source(outer);
        if (!source) *result = E_OUTOFMEMORY;
        return source;
    }

    DECLARE_IUNKNOWN;
    STDMETHODIMP NonDelegatingQueryInterface(REFIID id, void** out) override {
        if (id == IID_ISpecifyPropertyPages) return GetInterface(static_cast<ISpecifyPropertyPages*>(this), out);
        if (id == __uuidof(ISpecifyPropertyPages2)) return GetInterface(static_cast<ISpecifyPropertyPages2*>(this), out);
        if (id == __uuidof(IXocnSettings)) return GetInterface(static_cast<IXocnSettings*>(this), out);
        if (id == IID_IFileSourceFilter) return GetInterface(static_cast<IFileSourceFilter*>(this), out);
        return CSource::NonDelegatingQueryInterface(id, out);
    }

    STDMETHODIMP CreatePage(const GUID& id, IPropertyPage** page) override {
        if (!page) return E_POINTER;
        *page = nullptr;
        return id == PageId ? CreateSettingsPage(page) : CLASS_E_CLASSNOTAVAILABLE;
    }
    STDMETHODIMP GetPages(CAUUID* pages) override {
        if (!pages) return E_POINTER;
        pages->cElems = 0;
        pages->pElems = static_cast<GUID*>(CoTaskMemAlloc(sizeof(GUID)));
        if (!pages->pElems) return E_OUTOFMEMORY;
        pages->pElems[0] = PageId; pages->cElems = 1; return S_OK;
    }
    STDMETHODIMP GetInfo(unsigned* w, unsigned* h, unsigned* mx, unsigned* my, unsigned* ax, unsigned* ay, unsigned* fpsN, unsigned* fpsD) override {
        if (!w || !h || !mx || !my || !ax || !ay || !fpsN || !fpsD) return E_POINTER;
        CAutoLock lock(pStateLock());
        *w = decoder ? decoder->width : 0; *h = decoder ? decoder->height : 0;
        *mx = clip.aspectX; *my = clip.aspectY; *fpsN = clip.fpsNum; *fpsD = clip.fpsDen;
        *ax = clip.aspectX ? clip.aspectX : *w; *ay = clip.aspectY ? clip.aspectY : *h;
        if (squeeze && *w && *h) { *ax = *w * squeeze; *ay = *h * 1000; unsigned d = std::gcd(*ax, *ay); *ax /= d; *ay /= d; }
        return S_OK;
    }
    STDMETHODIMP GetMetadata(BSTR* name, unsigned* channels, unsigned* hz, unsigned* bits, BOOL* isFloat, ULONGLONG* frames, LONGLONG* duration) override {
        if (!name || !channels || !hz || !bits || !isFloat || !frames || !duration) return E_POINTER;
        CAutoLock lock(pStateLock());
        const auto separator = path.find_last_of(L"\\/");
        const wchar_t* filename = path.c_str() + (separator == std::wstring::npos ? 0 : separator + 1);
        *name = SysAllocString(filename);
        if (!*name) return E_OUTOFMEMORY;
        *channels = clip.channels; *hz = clip.rate; *bits = clip.bits; *isFloat = clip.floatAudio;
        *frames = clip.video.size(); *duration = clip.Time(clip.video.size());
        return S_OK;
    }
    STDMETHODIMP SetSqueeze(unsigned value) override {
        if (value && (value < 500 || value > 3000)) return E_INVALIDARG;
        HKEY key = nullptr;
        LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, SettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (result == ERROR_SUCCESS) { result = RegSetValueExW(key, L"Squeeze", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)); RegCloseKey(key); }
        return HRESULT_FROM_WIN32(result);
    }
    STDMETHODIMP SetDisplayLut(BOOL enabled) override {
        HKEY key = nullptr;
        LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, SettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (result == ERROR_SUCCESS) {
            DWORD value = enabled ? 1 : 0;
            result = RegSetValueExW(key, L"DisplayLut", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
            RegCloseKey(key);
        }
        if (result == ERROR_SUCCESS) {
            CAutoLock lock(pStateLock());
            displayLut = enabled != FALSE;
            if (decoder) decoder->SetDisplayLut(displayLut);
        }
        return HRESULT_FROM_WIN32(result);
    }
    STDMETHODIMP Load(LPCOLESTR filename, const AM_MEDIA_TYPE*) override {
        if (!filename) return E_POINTER;
        CAutoLock lock(pStateLock());
        if (GetPinCount()) return VFW_E_WRONG_STATE;
        path.clear();
        try {
            std::wstring candidatePath(filename);
            auto input = Open(filename);
            if (!input) return HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
            Clip candidate;
            if (!candidate.Load(input.get())) return VFW_E_INVALID_FILE_FORMAT;
            Bytes first;
            if (!Read(input.get(), candidate.video.front(), first)) return VFW_E_INVALID_FILE_FORMAT;
            auto newDecoder = std::make_unique<Decoder>();
            HRESULT hr = newDecoder->Init(first, candidate, displayLut, true);
            if (FAILED(hr)) return hr;
            auto audioInput = candidate.audioBytes ? Open(filename) : File(nullptr, fclose);
            if (candidate.audioBytes && !audioInput) return HRESULT_FROM_WIN32(ERROR_OPEN_FAILED);
            clip = std::move(candidate);
            decoder = std::move(newDecoder);
            auto* video = new Pin(&hr, this, clip, std::move(input), decoder.get(), squeeze);
            if (FAILED(hr)) {
                delete video;
                return hr;
            }
            if (audioInput) {
                auto* audio = new Pin(&hr, this, clip, std::move(audioInput), nullptr, squeeze);
                if (FAILED(hr)) {
                    delete audio;
                    delete video;
                    return hr;
                }
            }
            path = std::move(candidatePath);
            return hr;
        } catch (const std::bad_alloc&) {
            while (GetPinCount()) delete GetPin(GetPinCount() - 1);
            return E_OUTOFMEMORY;
        }
    }

    STDMETHODIMP GetCurFile(LPOLESTR* filename, AM_MEDIA_TYPE* media) override {
        if (!filename) return E_POINTER;
        CAutoLock lock(pStateLock());
        if (media) {
            *media = {};
            media->majortype = MEDIATYPE_Stream;
        }
        const size_t bytes = (path.size() + 1) * sizeof(wchar_t);
        *filename = static_cast<LPOLESTR>(CoTaskMemAlloc(bytes));
        if (!*filename) return E_OUTOFMEMORY;
        memcpy(*filename, path.c_str(), bytes);
        return S_OK;
    }
};

class SettingsPage final : public CBasePropertyPage {
    IXocnSettings* settings = nullptr;
public:
    SettingsPage(LPUNKNOWN outer) : CBasePropertyPage(NAME("X-OCN settings"), outer, 101, 102) {}
    static CUnknown* WINAPI Create(LPUNKNOWN outer, HRESULT* result) {
        auto* page = new (std::nothrow) SettingsPage(outer);
        if (!page) *result = E_OUTOFMEMORY;
        return page;
    }
    HRESULT OnConnect(IUnknown* object) override { return object->QueryInterface(__uuidof(IXocnSettings), reinterpret_cast<void**>(&settings)); }
    HRESULT OnDisconnect() override { if (settings) settings->Release(); settings = nullptr; return S_OK; }
    HRESULT OnActivate() override {
        unsigned w=0,h=0,mx=0,my=0,ax=0,ay=0,n=0,d=0;
        if (!settings || FAILED(settings->GetInfo(&w,&h,&mx,&my,&ax,&ay,&n,&d))) return E_FAIL;
        wchar_t line[512];
        BSTR name = nullptr; unsigned channels=0,hz=0,bits=0; BOOL isFloat=FALSE; ULONGLONG frames=0; LONGLONG duration=0;
        HRESULT hr = settings->GetMetadata(&name, &channels, &hz, &bits, &isFloat, &frames, &duration);
        if (FAILED(hr)) return hr;
        SetDlgItemTextW(m_Dlg, 1004, name); SysFreeString(name);
        swprintf_s(line, L"Frames: %llu    Duration: %.2f s", frames, double(duration) / 10000000.0);
        SetDlgItemTextW(m_Dlg, 1005, line);
        if (channels) swprintf_s(line, L"Audio: %u channels, %u Hz, %u-bit %s", channels, hz, bits, isFloat ? L"float" : L"PCM");
        else swprintf_s(line, L"Audio: none detected");
        SetDlgItemTextW(m_Dlg, 1006, line);
        swprintf_s(line, L"Frame: %u x %u    FPS: %.3f", w, h, d ? double(n)/d : 0.0);
        SetDlgItemTextW(m_Dlg, 1001, line);
        swprintf_s(line, L"MXF aspect: %u:%u    Output: %u:%u", mx,my,ax,ay);
        SetDlgItemTextW(m_Dlg, 1002, line);
        HWND combo = GetDlgItem(m_Dlg, 1003);
        const wchar_t* labels[] = {L"Automatic (MXF)",L"1.00x",L"1.25x",L"1.30x",L"1.33x",L"1.50x",L"1.80x",L"2.00x"};
        for (auto label : labels) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        unsigned value=0; DWORD bytes=sizeof(value);
        RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"Squeeze", RRF_RT_REG_DWORD, nullptr, &value, &bytes);
        const unsigned values[] = {0,1000,1250,1300,1333,1500,1800,2000};
        unsigned index=0; for (unsigned i=0;i<8;++i) if(values[i]==value) index=i;
        SendMessageW(combo, CB_SETCURSEL, index, 0);
        DWORD lut = 0; bytes = sizeof(lut);
        RegGetValueW(HKEY_CURRENT_USER, SettingsKey, L"DisplayLut", RRF_RT_REG_DWORD, nullptr, &lut, &bytes);
        CheckDlgButton(m_Dlg, 1007, lut == 1 ? BST_CHECKED : BST_UNCHECKED);
        return S_OK;
    }
    HRESULT OnApplyChanges() override {
        const unsigned values[] = {0,1000,1250,1300,1333,1500,1800,2000};
        const LRESULT index = SendDlgItemMessageW(m_Dlg, 1003, CB_GETCURSEL, 0, 0);
        if (index < 0 || index >= 8) return E_INVALIDARG;
        HRESULT hr = settings->SetSqueeze(values[index]);
        if (SUCCEEDED(hr)) MessageBoxW(m_Dlg, L"Close and reopen the clip to apply this setting.", L"X-OCN", MB_OK | MB_ICONINFORMATION);
        return hr;
    }
    INT_PTR OnReceiveMessage(HWND hwnd, UINT message, WPARAM w, LPARAM l) override {
        if (message == WM_COMMAND && LOWORD(w) == 1007 && HIWORD(w) == BN_CLICKED) {
            const bool checked = IsDlgButtonChecked(hwnd, 1007) == BST_CHECKED;
            const HRESULT hr = settings->SetDisplayLut(checked);
            if (FAILED(hr)) {
                CheckDlgButton(hwnd, 1007, checked ? BST_UNCHECKED : BST_CHECKED);
                MessageBoxW(hwnd, L"Could not save the display LUT setting.", L"X-OCN", MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        if (message == WM_COMMAND && LOWORD(w) == 1003 && HIWORD(w) == CBN_SELCHANGE) {
            m_bDirty = TRUE;
            if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
            return TRUE;
        }
        return CBasePropertyPage::OnReceiveMessage(hwnd,message,w,l);
    }
};

HRESULT CreateSettingsPage(IPropertyPage** page) {
    auto* instance = new (std::nothrow) SettingsPage(nullptr);
    if (!instance) return E_OUTOFMEMORY;
    const HRESULT hr = instance->NonDelegatingQueryInterface(IID_IPropertyPage, reinterpret_cast<void**>(page));
    if (FAILED(hr)) delete instance;
    return hr;
}

CFactoryTemplate g_Templates[] = {{FilterName, &FilterId, Source::Create, nullptr, nullptr}, {L"X-OCN Settings", &PageId, SettingsPage::Create, nullptr, nullptr}};
int g_cTemplates = 2;

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);
BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID reserved) {
    return DllEntryPoint(dll, reason, reserved);
}

