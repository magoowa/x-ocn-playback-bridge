#include <dshow.h>
#include <dvdmedia.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>

const CLSID nullId{0xc1f400a4,0x3f08,0x11d3,{0x9f,0x0b,0x00,0x60,0x08,0x03,0x9e,0x37}};
const CLSID id{0x927eb97c, 0xda96, 0x4af7, {0x9d,0x70,0xf3,0x14,0x32,0x67,0x48,0x62}};
void Check(HRESULT hr, const char* op) {
    std::printf("%s: 0x%08lX\n", op, hr);
    std::fflush(stdout);
    if (FAILED(hr)) std::exit(1);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 6) return 2;
    Check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "CoInitializeEx");
    Microsoft::WRL::ComPtr<IBaseFilter> source;
    Check(CoCreateInstance(id, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&source)), "CoCreateInstance");
    Microsoft::WRL::ComPtr<IFileSourceFilter> file; source.As(&file);
    if (!file) return 1;
    Check(file->Load(argv[1], nullptr), "Load");
    Microsoft::WRL::ComPtr<IGraphBuilder> graph;
    Check(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)), "FilterGraph");
    Check(graph->AddFilter(source.Get(), L"X-OCN"), "AddFilter");
    Microsoft::WRL::ComPtr<IEnumPins> pins;
    Check(source->EnumPins(&pins), "EnumPins");
    unsigned streams = 0;
    for (;;) {
        Microsoft::WRL::ComPtr<IPin> output;
        if (pins->Next(1, &output, nullptr) != S_OK) break;
        Microsoft::WRL::ComPtr<IBaseFilter> nullRenderer;
        Check(CoCreateInstance(nullId, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&nullRenderer)), "NullRenderer");
        Check(graph->AddFilter(nullRenderer.Get(), L"Null Renderer"), "AddNullRenderer");
        Microsoft::WRL::ComPtr<IEnumPins> inputs;
        Check(nullRenderer->EnumPins(&inputs), "EnumInputs");
        Microsoft::WRL::ComPtr<IPin> input;
        Check(inputs->Next(1, &input, nullptr), "GetInput");
        Check(graph->ConnectDirect(output.Get(), input.Get(), nullptr), "Connect");
        AM_MEDIA_TYPE media{};
        Check(output->ConnectionMediaType(&media), "ConnectionMediaType");
        if (media.majortype == MEDIATYPE_Video) {
            if (media.formattype != FORMAT_VideoInfo2 || media.cbFormat < sizeof(VIDEOINFOHEADER2)) return 1;
            const auto* video = reinterpret_cast<const VIDEOINFOHEADER2*>(media.pbFormat);
            std::printf("Video %ldx%ld, display %lu:%lu\n", video->bmiHeader.biWidth, video->bmiHeader.biHeight,
                        video->dwPictAspectRatioX, video->dwPictAspectRatioY);
            if (video->bmiHeader.biWidth != std::wcstol(argv[2], nullptr, 10) ||
                video->bmiHeader.biHeight != std::wcstol(argv[3], nullptr, 10) ||
                video->dwPictAspectRatioX != std::wcstoul(argv[4], nullptr, 10) ||
                video->dwPictAspectRatioY != std::wcstoul(argv[5], nullptr, 10)) return 1;
        }
        CoTaskMemFree(media.pbFormat);
        if (media.pUnk) media.pUnk->Release();
        ++streams;
    }
    std::printf("Streams: %u\n", streams);
    if (streams != 2) return 1;
    Microsoft::WRL::ComPtr<IMediaControl> control; graph.As(&control);
    Microsoft::WRL::ComPtr<IMediaSeeking> seeking; graph.As(&seeking);
    Microsoft::WRL::ComPtr<IMediaEvent> events; graph.As(&events);
    REFERENCE_TIME duration = 0;
    Check(seeking->GetDuration(&duration), "Duration");
    std::printf("Duration %.3fs\n", duration / 10000000.0);
    Check(control->Run(), "Run");
    Sleep(200);
    REFERENCE_TIME start = duration / 2, stop = start + 1000000;
    Check(seeking->SetPositions(&start, AM_SEEKING_AbsolutePositioning, &stop, AM_SEEKING_AbsolutePositioning), "Seek");
    long result = 0;
    Check(events->WaitForCompletion(6000, &result), "WaitForCompletion");
    std::printf("Completion event: %ld\n", result);
    Check(control->Stop(), "Stop");
    return result == EC_COMPLETE ? 0 : 1;
}


