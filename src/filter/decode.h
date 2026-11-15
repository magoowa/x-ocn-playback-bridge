#pragma once
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

struct Item {
    int64_t offset;
    uint32_t size;
};
using Bytes = std::vector<uint8_t>;
using File = std::unique_ptr<FILE, decltype(&fclose)>;
inline File Open(const wchar_t* path) { return File(_wfopen(path, L"rb"), fclose); }

inline uint32_t BE(const uint8_t* data, unsigned count) {
    uint32_t value = 0;
    while (count--) value = (value << 8) | *data++;
    return value;
}

inline bool Read(FILE* f, Item item, Bytes& data) {
    data.resize(item.size);
    return !_fseeki64(f, item.offset, SEEK_SET) &&
           fread(data.data(), 1, data.size(), f) == data.size();
}

inline uint8_t SLog3Code(unsigned code, unsigned black, unsigned gray) {
    if (gray <= black) return 0;
    const double scene = (std::max)(0.0, (double(code) - black) * 0.18 / double(gray - black));
    const double value = scene >= 0.01125
        ? (420.0 + std::log10((scene + 0.01) / 0.19) * 261.5) / 1023.0
        : (95.0 + scene * (171.2102946929 - 95.0) / 0.01125) / 1023.0;
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255));
}

struct Clip {
    std::vector<Item> video;
    std::array<std::vector<Item>, 256> tracks;
    unsigned fpsNum = 0, fpsDen = 0;
    unsigned aspectX = 0, aspectY = 0;
    unsigned rawBlack = 0, rawGray = 0;
    unsigned cropX = 0, cropY = 0, cropW = 0, cropH = 0;
    unsigned rate = 0, bits = 0, channels = 0, audioBytes = 0;
    bool floatAudio = false;
    std::string cameraModel, lookReference;

    void ReadLookReference(const Bytes& metadata) {
        if (!lookReference.empty()) return;
        const std::string text(metadata.begin(), metadata.end());
        for (const char* prefix : {"LUT:", "EMB1:"}) {
            auto start = text.find(prefix);
            if (start == std::string::npos) continue;
            const auto end = text.find(".cube", start);
            if (end == std::string::npos || end - start > 100) continue;
            auto value = text.substr(start, end + 5 - start);
            if (std::all_of(value.begin(), value.end(), [](unsigned char c) {
                return (c >= ' ' && c <= '~' && c != '"' && c != '<' && c != '>');
            })) {
                lookReference = std::move(value);
                return;
            }
        }
    }

    void ReadCameraModel(const std::string& text) {
        const auto device = text.find("<Device ");
        const auto end = text.find('>', device);
        auto model = text.find("modelName=\"", device);
        if (model != std::string::npos && model < end) {
            model += 11;
            const auto close = text.find('"', model);
            if (close != std::string::npos && close < end && close - model <= 96) {
                auto value = text.substr(model, close - model);
                if (!value.empty() && value != "MODEL-NAME") cameraModel = std::move(value);
            }
        }
        if (cameraModel.empty()) {
            const auto name = text.find("name=\"CameraAttributes\"");
            const auto item = text.rfind('<', name);
            const auto itemEnd = text.find('>', name);
            const auto value = text.find("value=\"", item);
            if (name != std::string::npos && item != std::string::npos &&
                itemEnd != std::string::npos && value != std::string::npos && value < itemEnd) {
                const auto start = value + 7;
                const auto close = text.find('"', start);
                if (close != std::string::npos && close < itemEnd) {
                    const auto tokenEnd = text.find(' ', start);
                    const auto length = (std::min)(close, tokenEnd) - start;
                    if (length && length <= 96) {
                        auto model = text.substr(start, length);
                        if (model != "MODEL-NAME") cameraModel = std::move(model);
                    }
                }
            }
        }
        if (cameraModel == "MPC-2610" || cameraModel == "BURANO/CineAltaB") cameraModel = "BURANO (MPC-2610)";
        if (cameraModel == "MPC-3628") cameraModel = "VENICE 2 8.6K (MPC-3628)";
        if (cameraModel == "MPC-3626") cameraModel = "VENICE 2 6K (MPC-3626)";
        if (cameraModel == "MPC-3610") cameraModel = "VENICE 1 (MPC-3610)";
    }

    void Descriptor(const Bytes& data, bool picture) {
        for (size_t at = 0; at + 4 <= data.size();) {
            const unsigned tag = BE(&data[at], 2), size = BE(&data[at + 2], 2);
            at += 4;
            if (size > data.size() - at) break;
            const auto* v = data.data() + at;
            if (picture) {
                if (tag == 0x3001 && size == 8) {
                    fpsNum = BE(v, 4);
                    fpsDen = BE(v + 4, 4);
                }
                if (tag == 0x320E && size == 8) {
                    const unsigned x = BE(v, 4), y = BE(v + 4, 4);
                    if (x && y && x <= 65535 && y <= 65535) {
                        aspectX = x;
                        aspectY = y;
                    }
                }
                if (size == 4) {
                    switch (tag) {
                    case 0x3208: cropH = BE(v, 4); break;
                    case 0x3209: cropW = BE(v, 4); break;
                    case 0x320A: cropX = BE(v, 4); break;
                    case 0x320B: cropY = BE(v, 4); break;
                    }
                }
            } else {
                if (tag == 0x3D03 && size == 8 && BE(v + 4, 4)) rate = BE(v, 4) / BE(v + 4, 4);
                if (tag == 0x3D01 && size == 4) bits = BE(v, 4);
                if (tag == 0x3D07 && size == 4) channels = BE(v, 4);
            }
            at += size;
        }
    }

    bool Load(FILE* f) {
        if (_fseeki64(f, 0, SEEK_END)) return false;
        const auto end = _ftelli64(f);
        if (end < 0) return false;
        bool floatLabel = false;
        Bytes data;
        // shortcut: inspect the first 4 MiB for Sony's embedded XML; support sidecars if absent on more cameras.
        const size_t headerBytes = size_t((std::min)(int64_t(4u << 20), end));
        Bytes header(headerBytes);
        if (_fseeki64(f, 0, SEEK_SET) || fread(header.data(), 1, header.size(), f) != header.size()) return false;
        const std::string text(header.begin(), header.end());
        ReadCameraModel(text);
        auto readCode = [&](const char* name) -> unsigned {
            const auto tag = text.find(std::string("name=\"") + name + "\"");
            if (tag == std::string::npos) return 0;
            const auto tagEnd = text.find('>', tag);
            const auto value = text.find("value=\"", tag);
            if (value == std::string::npos || tagEnd == std::string::npos || value > tagEnd) return 0;
            size_t pos = value + 7; unsigned result = 0, digits = 0;
            while (pos < tagEnd && text[pos] >= '0' && text[pos] <= '9' && digits < 6) {
                result = result * 10 + unsigned(text[pos++] - '0'); ++digits;
            }
            return digits && pos < tagEnd && text[pos] == '\"' && result <= 65535 ? result : 0;
        };
        rawBlack = readCode("RawBlackCodeValue");
        rawGray = readCode("RawGrayCodeValue");
        if (rawGray <= rawBlack) { rawBlack = 0; rawGray = 0; }
        for (int64_t offset = 0; offset < end;) {
            // shortcut: camera MXFs place the first picture near the header.
            // Add MXF index parsing if clips with longer run-ins need support.
            if (video.empty() && offset > (64ll << 20)) return false;
            uint8_t key[16];
            if (_fseeki64(f, offset, SEEK_SET) || fread(key, 1, 16, f) != 16 ||
                BE(key, 4) != 0x060E2B34) return false;
            const int first = fgetc(f);
            if (first == EOF || first == 128 || first > 136) return false;
            uint64_t length = first;
            if (first > 128) {
                length = 0;
                for (int n = first - 128; n; --n) {
                    const int byte = fgetc(f);
                    if (byte == EOF) return false;
                    length = (length << 8) | byte;
                }
            }
            const int64_t value = _ftelli64(f);
            if (value < 0 || length > uint64_t(end - value)) return false;
            offset = value + int64_t(length);

            const auto type = BE(key + 4, 4);
            const auto label = BE(key + 8, 4);
            const Item item{value, static_cast<uint32_t>(length)};
            if (type == 0x01020106 && (label >> 16) == 0x0e06) {
                if (length < 64 || length > (64u << 20) || video.size() >= 4000000) return false;
                video.push_back(item);
            } else if (type == 0x01020101 && label == 0x0d010301 && key[12] == 0x16) {
                if (length > (64u << 10) || tracks[key[15]].size() >= 4000000) return false;
                tracks[key[15]].push_back(item);
            } else if (type == 0x02530101 && label == 0x0d010101 && length <= 65536) {
                const bool picture = key[14] == 0x27 || key[14] == 0x28 || key[14] == 0x29;
                const bool sound = key[14] == 0x42 || key[14] == 0x47 || key[14] == 0x48;
                if ((picture && !fpsNum) || (sound && !bits)) {
                    if (!Read(f, item, data)) return false;
                    Descriptor(data, picture);
                }
            } else if (offset <= (16 << 20) && length && length <= 65536) {
                if (!Read(f, item, data)) return false;
                ReadLookReference(data);
                static constexpr char labelText[] = "audioCodec=\"FLOAT32\"";
                floatLabel |= std::search(data.begin(), data.end(), labelText,
                                          labelText + sizeof(labelText) - 1) != data.end();
            }
        }
        if (video.empty() || !fpsNum || !fpsDen || fpsNum > 1000000 ||
            fpsDen > 100000 || fpsNum < fpsDen || fpsNum > 240ull * fpsDen) return false;

        unsigned trackCount = 0, maxPacket = 0;
        const std::vector<Item>* firstTrack = nullptr;
        bool aligned = true;
        for (const auto& track : tracks) {
            if (track.empty()) continue;
            ++trackCount;
            aligned &= track.size() == video.size();
            if (!firstTrack) firstTrack = &track;
            if (!aligned) continue;
            for (size_t n = 0; n < track.size(); ++n) {
                aligned &= track[n].size == (*firstTrack)[n].size;
                aligned &= bits >= 16 && track[n].size % (bits / 8) == 0;
                if (bits >= 16 && rate == 48000) {
                    const auto samples = track[n].size / (bits / 8);
                    const auto expected = uint64_t(rate) * fpsDen / fpsNum;
                    aligned &= samples >= expected && samples <= expected + 1;
                }
                maxPacket = std::max(maxPacket, track[n].size);
            }
        }
        floatAudio = bits == 32 && floatLabel;
        if (aligned && channels == 1 && trackCount <= 8 && rate == 48000 &&
            (bits == 16 || bits == 24 || floatAudio)) {
            channels = trackCount;
            audioBytes = maxPacket * channels;
        }
        return true;
    }

    int64_t Time(size_t frame) const {
        return static_cast<int64_t>(frame * (10000000ull * fpsDen) / fpsNum);
    }

    size_t Frame(int64_t time) const {
        if (time <= 0) return 0;
        if (time >= Time(video.size())) return video.size();
        size_t low = 0, high = video.size();
        while (low + 1 < high) {
            const size_t mid = low + (high - low) / 2;
            if (Time(mid) <= time) low = mid;
            else high = mid;
        }
        return low;
    }
};

class Decoder {
    struct Buffer {
        void* data[3]{};
        unsigned size[3]{};
    };
    static_assert(sizeof(Buffer) == 40);
    using Create = void* (*)();
    using Destroy = void (*)(void*);
    using Group = int (*)(void*, const void*, unsigned, int*);
    using Control = int (*)(void*, const void*);
    using Attribute = int (*)(void*, const void*, void*);
    using Develop = int (*)(void*, const void*, unsigned, Buffer*);

    HMODULE module = nullptr;
    void* dev = nullptr;
    Destroy destroy = nullptr;
    Develop develop = nullptr;
    Buffer buffer;
    Buffer gpuBuffer;
    void* gpuDev = nullptr;
    Create createDevice = nullptr;
    Control initializeDevice = nullptr;
    std::array<unsigned, 22> deviceSettings{};
    std::array<std::vector<uint16_t>, 3> planes;
    std::array<std::array<uint8_t, 65536>, 2> display{};
    std::atomic<unsigned> displayMode{0};
    unsigned storedW = 0, storedH = 0, left = 0, top = 0;
    uint32_t magic = 0;

    struct RenderBand {
        const Decoder* decoder = nullptr;
        PTP_WORK work = nullptr;
        uint8_t* output = nullptr;
        unsigned first = 0, end = 0;
        bool half = false;
        const uint8_t* table = nullptr;
    };
    std::vector<RenderBand> bands;

    static void CALLBACK RenderWorker(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) {
        const auto& band = *static_cast<const RenderBand*>(context);
        if (band.half) band.decoder->RenderHalfRows(band.output, band.first / 2, band.end / 2, band.table);
        else band.decoder->RenderRows(band.output, band.first, band.end, band.table);
    }

    template<class T> T Symbol(const char* name) const {
        return reinterpret_cast<T>(GetProcAddress(module, name));
    }

public:
    unsigned width = 0, height = 0;
    unsigned Stride() const { return (width * 3 + 3) & ~3u; }
    unsigned Size() const { return Stride() * height; }
    unsigned StoredWidth() const { return storedW; }
    unsigned StoredHeight() const { return storedH; }
    unsigned CropLeft() const { return left; }
    unsigned CropTop() const { return top; }
    const uint16_t* Plane(unsigned index) const { return index < planes.size() ? planes[index].data() : nullptr; }
    const uint8_t* DisplayTable() const { return display[displayMode.load(std::memory_order_acquire)].data(); }

    bool CanPreviewXt() const {
        return deviceSettings[1] == 9 && width == 8192 && height == 4320;
    }

    bool StartGpu(void* device, void* context, void* const* buffers, unsigned sonyScale) {
        auto settings = deviceSettings;
        if (sonyScale) {
            // SonyRawDev 6.0 Control6 mode observed in Catalyst's XT playback.
            settings = {1, deviceSettings[1], storedW, storedH, width, height,
                        sonyScale == 4 ? 0x300u : 0x100u, 0, 0, 10000, 0, 0x3f800000, 2};
            const auto attribute = Symbol<Attribute>("?getAttribute@SonyRawDev@sony_raw_dev@@UEBAHPEBURawDevControl6@2@PEAURawDevAttribute@2@@Z");
            if (!attribute) return false;
            settings[14] = settings[16] = 1;
            memcpy(reinterpret_cast<unsigned char*>(settings.data()) + 72, &device, sizeof(device));
            memcpy(reinterpret_cast<unsigned char*>(settings.data()) + 80, &context, sizeof(context));
            alignas(16) unsigned attributes[2048]{};
            if (attribute(dev, settings.data(), attributes) || attributes[6] != width / sonyScale ||
                attributes[9] != height / sonyScale || attributes[12] != 8) return false;
        }
        settings[14] = 1;
        settings[16] = 1;
        memcpy(reinterpret_cast<unsigned char*>(settings.data()) + 72, &device, sizeof(device));
        memcpy(reinterpret_cast<unsigned char*>(settings.data()) + 80, &context, sizeof(context));
        gpuDev = createDevice();
        if (!gpuDev || initializeDevice(gpuDev, settings.data())) {
            StopGpu();
            return false;
        }
        if (sonyScale) {
            gpuBuffer = {};
            gpuBuffer.data[0] = buffers[0];
            gpuBuffer.size[0] = (width / sonyScale) * (height / sonyScale) * 8;
            return true;
        }
        gpuBuffer = buffer;
        std::copy_n(buffers, 3, gpuBuffer.data);
        return true;
    }
    void StopGpu() { if (gpuDev) { destroy(gpuDev); gpuDev = nullptr; } }
    HRESULT DecodeGpu(const Bytes& frame) {
        if (frame.size() < 64 || BE(frame.data(), 4) != magic ||
            BE(&frame[8], 2) != storedW || 2 * BE(&frame[10], 2) != storedH)
            return VFW_E_INVALID_FILE_FORMAT;
        return develop(gpuDev, frame.data(), unsigned(frame.size()), &gpuBuffer) ? E_FAIL : S_OK;
    }

    Decoder() = default;
    Decoder(const Decoder&) = delete;
    ~Decoder() {
        for (const auto& band : bands)
            if (band.work) CloseThreadpoolWork(band.work);
        StopGpu();
        if (dev) destroy(dev);
        if (module) FreeLibrary(module);
    }

    HRESULT Init(const Bytes& frame, const Clip& clip, bool displayLut = true, bool neutralLog = false) {
        if (frame.size() < 64 || BE(frame.data(), 3) != 0x534252) return VFW_E_INVALID_FILE_FORMAT;
        magic = BE(frame.data(), 4);
        storedW = BE(&frame[8], 2);
        storedH = 2 * BE(&frame[10], 2);
        if (!storedW || !storedH || (storedW & 1) || storedW > 10000 || storedH > 6500)
            return VFW_E_INVALID_FILE_FORMAT;
        width = storedW;
        height = storedH;
        if (clip.cropW && clip.cropH &&
            uint64_t(clip.cropX) + clip.cropW <= storedW &&
            uint64_t(clip.cropY) + clip.cropH <= storedH) {
            left = clip.cropX;
            top = clip.cropY;
            width = clip.cropW;
            height = clip.cropH;
        }
        constexpr const wchar_t* catalyst = L"C:\\Program Files\\Sony\\Catalyst Prepare V3\\SonyRawDev.dll";
        constexpr const wchar_t* viewer = L"C:\\Program Files\\Sony\\RAW Viewer\\SonyRawDev.dll";
        wchar_t configured[MAX_PATH * 4]{};
        DWORD configuredBytes = sizeof(configured);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\XOCN-MPC", L"SonyRawDevPath", RRF_RT_REG_SZ,
                         nullptr, configured, &configuredBytes) == ERROR_SUCCESS && configured[0] ||
            (configuredBytes = sizeof(configured), RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\XOCN-MPC", L"SonyRawDevPath",
                RRF_RT_REG_SZ, nullptr, configured, &configuredBytes) == ERROR_SUCCESS && configured[0])) {
            module = LoadLibraryExW(configured, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!module) return HRESULT_FROM_WIN32(GetLastError());
        } else {
            module = LoadLibraryExW(catalyst, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!module)
            module = LoadLibraryExW(viewer, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }
        if (!module) return HRESULT_FROM_WIN32(GetLastError());

        const auto create = Symbol<Create>("createSonyRawDev");
        destroy = Symbol<Destroy>("destroySonyRawDev");
        const auto group = Symbol<Group>("?getBitstreamGroup@SonyRawDev@sony_raw_dev@@UEBAHPEBXIPEAW4BITSTREAM_GROUP@2@@Z");
        const auto init = Symbol<Control>("?initialize@SonyRawDev@sony_raw_dev@@UEAAHPEBURawDevControl6@2@@Z");
        const auto attribute = Symbol<Attribute>("?getAttribute@SonyRawDev@sony_raw_dev@@UEBAHPEBURawDevControl6@2@PEAURawDevAttribute@2@@Z");
        develop = Symbol<Develop>("?develop@SonyRawDev@sony_raw_dev@@UEAAHPEBXIPEAURawDevBuffer@2@@Z");
        if (!create || !destroy || !group || !init || !attribute || !develop)
            return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        dev = create();
        int family = -1;
        if (!dev || group(dev, frame.data(), unsigned(frame.size()), &family) || family < 0)
            return VFW_E_INVALID_FILE_FORMAT;

        // Windows Control6 layout validated with the installed 6.0.0.0 DLL.
        alignas(16) unsigned settings[22]{0x6c6c7663, unsigned(family),
            storedW, storedH, storedW, storedH, 0x100};
        alignas(16) uint8_t attributes[8192]{};
        int status = attribute(dev, settings, attributes);
        if (status && (width != storedW || height != storedH)) {
            // Legacy X-OCN groups look up the stored and active raster as a pair.
            settings[4] = width;
            settings[5] = height;
            status = attribute(dev, settings, attributes);
        }
        if (status || init(dev, settings)) return VFW_E_INVALID_FILE_FORMAT;
        createDevice = create;
        initializeDevice = init;
        std::copy_n(settings, 22, deviceSettings.begin());
        for (unsigned n = 0; n < 3; ++n) {
            // Reject plane layouts other than tightly packed 16-bit Bayer planes.
            const auto expectedW = n == 1 ? storedW : storedW / 2;
            unsigned actualW, actualH, bytes;
            memcpy(&actualW, attributes + 0x18 + 4 * n, 4);
            memcpy(&actualH, attributes + 0x24 + 4 * n, 4);
            memcpy(&bytes, attributes + 0x30 + 4 * n, 4);
            if (actualW != expectedW || actualH != storedH / 2 || bytes != 2)
                return VFW_E_INVALID_FILE_FORMAT;
            const size_t samples = size_t(actualW) * actualH;
            planes[n].resize(samples);
            buffer.data[n] = planes[n].data();
            buffer.size[n] = static_cast<unsigned>(samples * 2);
        }
        // shortcut: missing raw code points use FX5 levels; use per-camera calibration when metadata is absent.
        const unsigned black = clip.rawGray > clip.rawBlack ? clip.rawBlack : 512;
        const unsigned gray = clip.rawGray > clip.rawBlack ? clip.rawGray : 1504;
        for (size_t i = 0; i < display[0].size(); ++i) {
            const double scene = (std::max)(0.0, (double(i) - black) * 0.18 / double(gray - black));
            uint8_t logValue = SLog3Code(static_cast<unsigned>(i), black, gray);
            const double soft = scene / (1.0 + scene);
            const double preview = soft <= .0031308 ? 12.92 * soft : 1.055 * std::pow(soft, 1 / 2.4) - .055;
            if (!neutralLog) {
                const double legacy = std::clamp((double(i) - 512.0) / 16384.0, 0.0, 1.0);
                logValue = static_cast<uint8_t>(std::lround(legacy * 255));
                display[1][i] = static_cast<uint8_t>(std::lround((legacy <= .0031308 ? 12.92 * legacy : 1.055 * std::pow(legacy, 1 / 2.4) - .055) * 255));
            } else {
                display[1][i] = static_cast<uint8_t>(std::lround(std::clamp(preview, 0.0, 1.0) * 255));
            }
            display[0][i] = logValue;
        }
        SetDisplayLut(displayLut);
        const unsigned bandCount = width >= 7680 ? 8 : 4;
        bands.resize(bandCount - 1);
        for (unsigned n = 0; n < bands.size(); ++n) {
            auto& band = bands[n];
            band.decoder = this;
            band.first = height * (n + 1) / bandCount;
            band.end = height * (n + 2) / bandCount;
            band.work = CreateThreadpoolWork(RenderWorker, &band, nullptr);
        }
        return Decode(frame);
    }

    HRESULT Decode(const Bytes& frame) {
        if (frame.size() < 64 || BE(frame.data(), 4) != magic ||
            BE(&frame[8], 2) != storedW || 2 * BE(&frame[10], 2) != storedH)
            return VFW_E_INVALID_FILE_FORMAT;
        return develop(dev, frame.data(), unsigned(frame.size()), &buffer) ? E_FAIL : S_OK;
    }

    void SetDisplayLut(bool enabled) { displayMode.store(enabled ? 1u : 0u, std::memory_order_release); }
    void Render(uint8_t* output) {
        const auto* table = display[displayMode.load(std::memory_order_acquire)].data();
        for (auto& band : bands) {
            band.table = table;
            band.output = output;
            band.half = false;
            if (band.work) SubmitThreadpoolWork(band.work);
        }
        RenderRows(output, 0, bands.front().first, table);
        for (const auto& band : bands) {
            if (band.work) WaitForThreadpoolWorkCallbacks(band.work, FALSE);
            else RenderRows(output, band.first, band.end, table);
        }
    }

    bool CanRenderHalf() const { return !(left & 1) && !(top & 1) && !(width & 1) && !(height & 1); }
    void RenderHalf(uint8_t* output) {
        const auto* table = display[displayMode.load(std::memory_order_acquire)].data();
        for (auto& band : bands) {
            band.table = table;
            band.output = output; band.half = true;
            if (band.work) SubmitThreadpoolWork(band.work);
        }
        RenderHalfRows(output, 0, bands.front().first / 2, table);
        for (const auto& band : bands) {
            if (band.work) WaitForThreadpoolWorkCallbacks(band.work, FALSE);
            else RenderHalfRows(output, band.first / 2, band.end / 2, table);
        }
    }
    void RenderHalfRows(uint8_t* output, unsigned first, unsigned end, const uint8_t* table) const {
        const unsigned outW = width / 2, outH = height / 2;
        const unsigned stride = (outW * 3 + 3) & ~3u;
        for (unsigned y = first; y < end; ++y) {
            const size_t row = size_t(top / 2 + y);
            const auto* red = planes[0].data() + row * (storedW / 2) + left / 2;
            const auto* blue = planes[2].data() + row * (storedW / 2) + left / 2;
            const auto* green = planes[1].data() + row * storedW + left;
            auto* pixel = output + size_t(outH - 1 - y) * stride;
            for (unsigned x = 0; x < outW; ++x) {
                *pixel++ = table[blue[x]];
                *pixel++ = table[(unsigned(green[2*x]) + green[2*x+1]) / 2];
                *pixel++ = table[red[x]];
            }
            std::fill_n(pixel, stride - 3 * outW, 0);
        }
    }

private:
    void RenderRows(uint8_t* output, unsigned first, unsigned end, const uint8_t* table) const {
        const int halfW = static_cast<int>(storedW / 2);
        const int halfH = static_cast<int>(storedH / 2);
        for (unsigned y = first; y < end; ++y) {
            const unsigned sensorY = top + y;
            const int row = static_cast<int>(sensorY / 2);
            const bool lower = (sensorY & 1) != 0;
            const int neighbor = std::clamp(row + (lower ? 1 : -1), 0, halfH - 1);
            const auto* red = planes[0].data() + size_t(row) * halfW;
            const auto* blue = planes[2].data() + size_t(row) * halfW;
            const auto* green = planes[1].data() + size_t(row) * storedW;
            const auto* redOther = planes[0].data() + size_t(neighbor) * halfW;
            const auto* blueOther = planes[2].data() + size_t(neighbor) * halfW;
            const auto* greenOther = planes[1].data() + size_t(neighbor) * storedW;
            const auto stride = Stride();
            auto* pixel = output + size_t(height - 1 - y) * stride;

            // Sony's G plane stores the bottom-left green before the top-right green.
            // Interpolate missing channels at each RGGB site in linear light.
            for (unsigned x = 0; x < width; ++x) {
                const unsigned sensorX = left + x;
                const int col = static_cast<int>(sensorX / 2);
                const bool right = (sensorX & 1) != 0;
                const int side = std::clamp(col + (right ? 1 : -1), 0, halfW - 1);
                unsigned r, g, b;
                if (!lower && !right) {
                    r = red[col];
                    g = (green[2 * side + 1] + green[2 * col + 1] +
                         greenOther[2 * col] + green[2 * col]) / 4;
                    b = (blueOther[side] + blueOther[col] + blue[side] + blue[col]) / 4;
                } else if (!lower) {
                    r = (red[col] + red[side]) / 2;
                    g = green[2 * col + 1];
                    b = (blueOther[col] + blue[col]) / 2;
                } else if (!right) {
                    r = (red[col] + redOther[col]) / 2;
                    g = green[2 * col];
                    b = (blue[side] + blue[col]) / 2;
                } else {
                    r = (red[col] + red[side] + redOther[col] + redOther[side]) / 4;
                    g = (green[2 * col] + green[2 * side] +
                         green[2 * col + 1] + greenOther[2 * col + 1]) / 4;
                    b = blue[col];
                }
                *pixel++ = table[b];
                *pixel++ = table[g];
                *pixel++ = table[r];
            }
            std::fill_n(pixel, stride - 3 * width, 0);
        }
    }
};
