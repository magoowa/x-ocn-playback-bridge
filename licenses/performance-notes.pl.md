# Akceleracja Sony X-OCN w MPC-HC

OpenCL nadaje się do przyspieszenia rekonstrukcji X-OCN. Przy obecnym wyjściu RGB24 w pamięci systemowej zmierzona przewaga nad CPU jest mała. Obecny filtr dekoduje skompresowany X-OCN na CPU do 16-bitowych płaszczyzn Bayera, a następnie wielowątkowo rekonstruuje z nich RGB24. W teście DirectShow z zegarem klip FX5 X-OCN LT 4992×3328 był renderowany z szybkością 25,00 kl./s. [Kod dekodera](src/decode.h) · [Wyjście DirectShow](src/filter.cpp)

## DXVA i D3D11 Video

Uruchomienie dekodera przez DXVA2 lub D3D11 Video wymaga profilu obsługiwanego przez sterownik. DXVA2 udostępnia listę identyfikatorów GUID przez `GetDecoderDeviceGuids`. W D3D11 Video `GetVideoDecoderProfile` zwraca GUID profilu, `CheckVideoDecoderFormat` sprawdza format wyjścia, a `GetVideoDecoderConfigCount` sprawdza dostępność konfiguracji dla opisu strumienia. Sam wybór DXVA w odtwarzaczu nie dodaje obsługi kodeka. [Microsoft DXVA2](https://learn.microsoft.com/en-us/windows/win32/api/dxva2api/nf-dxva2api-idirectxvideodecoderservice-getdecoderdeviceguids) · [Microsoft D3D11 Video](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nn-d3d11-id3d11videodevice)

Sony opisuje X-OCN jako własny format skompresowanego RAW i udostępnia jego obsługę w Sony RAW SDK. W opublikowanym wykazie kodeków dekodowanych przez NVIDIA NVDEC znajdują się m.in. H.264, HEVC i AV1. X-OCN nie figuruje w tym wykazie. [Sony X-OCN White Paper, s. 2 i 18](https://pro.sony/s3/2023/09/07140729/Sony_X-OCN_WhitePaper_v2.0.pdf) · [NVIDIA Video Codec SDK](https://developer.nvidia.com/video-codec-sdk)

W lokalnym teście RTX PRO 6000 Blackwell Max-Q obie funkcje zwróciły ten sam zestaw 34 GUID-ów. W nagłówkach Windows SDK 10.0.26100.0 można przypisać 25 z nich do rodzin MPEG, VC-1, H.264, HEVC, JPEG/MJPEG, VP9 i AV1. Pozostałych 9 nie zidentyfikowano na podstawie tych nagłówków. Test nie wykazał profilu X-OCN. [Nagłówki D3D11 publikowane przez Microsoft](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/d3d11.h)

## OpenCL i koszt przesyłania klatek

Lokalny `SonyRawDev.dll` 6.0.0.0 zawiera wariant `GpuFullDebayer`. Próba `isDeviceSupported` dla OpenCL na NVIDIA RTX PRO 6000 Blackwell Max-Q zwróciła kod `0` i wartość `true`. Próba potwierdza akceptację urządzenia przez bibliotekę. Przebieg dekodowania klatki i wydajność pełnej ścieżki GPU pozostają do zmierzenia. Przy osobnym kernelu OpenCL można przenieść na GPU etap Bayer → RGB. D3D11 Compute Shader również pozwala zaprogramować tę operację, używając HLSL i `Dispatch`. [Specyfikacja OpenCL](https://registry.khronos.org/OpenCL/specs/unified/html/OpenCL_API.html) · [Microsoft DirectCompute](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader)

Jeśli osobny kernel pobiera Bayer16 z RAM i zwraca RGB24 do obecnego wyjścia DirectShow w RAM, dla aktywnego rastra 4992×3328 przesyła około 31,7 MiB na GPU i 47,5 MiB z powrotem na klatkę. Daje to około 1,93 GiB/s łącznie przy 25 kl./s, przed uwzględnieniem synchronizacji, dodatkowych kopii i ewentualnie większego rastra zapisanego w pliku. Przy wyjściu na teksturę `cl_khr_d3d11_sharing` pozwala współdzielić obsługiwane zasoby OpenCL i D3D11. Oszczędność odczytu do RAM zależy od dostępności rozszerzenia, zgodności zasobu i przyjęcia tekstury przez dalszą część grafu. [Khronos o transferach buforów](https://registry.khronos.org/OpenCL/specs/unified/refpages/man/html/clEnqueueReadBuffer.html) · [Khronos o współdzieleniu D3D11](https://registry.khronos.org/OpenCL/specs/unified/refpages/man/html/cl_khr_d3d11_sharing.html)

Sterownik NVIDIA zgłasza rozszerzenie `cl_nv_d3d11_sharing`. Test lokalny odczytał je z listy rozszerzeń OpenCL urządzenia i platformy. W tym samym odczycie nie wystąpiło `cl_khr_d3d11_sharing`. Zgodność rozszerzenia NVIDIA z formatem tekstury i rendererem MPC-HC pozostaje do sprawdzenia.

## Wynik pomiaru na klipie LT

Z pierwszej klatki `M002C001_260101N7.mxf` uzyskano te same płaszczyzny Bayera przez SonyRawDev. Osobny kernel OpenCL odtworzył biliniową rekonstrukcję, kadrowanie i krzywą podglądu obecnego filtra. Bufory przydzielono raz. Pomiar objął 40 powtórzeń po 4 rozgrzewkowych. SHA-256 całej klatki RGB24 było identyczne dla obu wyjść.

| Operacja | Mediana | 95. percentyl |
| --- | ---: | ---: |
| Obecny wielowątkowy CPU, Bayer → RGB24 w RAM | 8,25 ms | 10,09 ms |
| OpenCL, upload Bayera, obliczenie, odczyt RGB24 do RAM | 7,91 ms | 9,53 ms |
| OpenCL, obliczenie z danymi już w GPU i synchronizacja | 0,09 ms | — |

Pomiar dotyczy rekonstrukcji pojedynczej klatki. Czas rozpakowania X-OCN, przekazania próbki do renderera i wyświetlenia pozostaje poza pomiarem. Wariant OpenCL działał w osobnym teście.

## Pomiar X-OCN XT 8K

`A002C069_220624H6.mxf` ma aktywny raster 8192×4320 przy 24 kl./s, raster zapisany 8256×4384 i 360 klatek. Do pierwszego benchmarku użyto buildu filtra przekazującego bibliotece Sony obie geometrie.

| Rekonstrukcja tej samej klatki, 40 powtórzeń | Mediana |
| --- | ---: |
| CPU, Bayer → RGB24 w RAM | 17,30 ms |
| OpenCL z uploadem Bayera i odczytem RGB24 do RAM, dwie próby | 14,34–15,64 ms |
| OpenCL z uploadem Bayera i wynikiem pozostawionym w GPU | 5,64 ms |
| Sam kernel OpenCL z synchronizacją | 0,21 ms |

Porównanie SHA-256 potwierdziło identyczną klatkę RGB24 z CPU i OpenCL. Wynik pozostawiony w pamięci GPU nie przechodził przez renderer MPC-HC. W oddzielnym przebiegu 40 kolejnych klatek średni odczyt obrazu z MXF trwał 72,31 ms na klatkę, rozpakowanie przez SonyRawDev 36,58 ms, a rekonstrukcja CPU 17,76 ms. Dla 24 kl./s dostępne jest 41,67 ms na klatkę, a obraz skompresowany wymaga odczytu około 483 MB/s.

Graf DirectShow z zegarem dostarczył 120 próbek obrazu i 120 próbek audio z identycznymi znacznikami czasu. Obraz osiągnął 9,86 kl./s, a maksymalny odstęp między odpowiadającymi próbkami w odbiornikach testowych wyniósł 7,12 s. Pomiar obejmował dysk E: i testowe odbiorniki DirectShow.

RAW Viewer wyeksportował po 12 klatek 8192×4320 do DPX 10-bit w dwóch próbach CPU i dwóch próbach Auto. CPU uzyskał 6,99–7,16 kl./s, a Auto 2,82–2,89 kl./s. Ten wynik obejmuje uruchomienie procesu i zapis DPX. Tryb Auto może sam wybierać urządzenie, więc ten pomiar nie identyfikuje pracy GPU.

### Strojenie filtra 8K

Sekwencyjny odczyt 768 MiB z pominięciem bufora Windows dał **243,3 MB/s** na `E:` i **4031 MB/s** na NVMe `C:`. Sam obraz tego klipu wymaga około **483 MB/s** przy 24 kl./s. Krótkie opóźnienia i przepustowość można oceniać niezależnie od dekodera.

W pomiarze 16 różnych pakietów w RAM automatyczny dobór wątków Sony dał medianę **15,3–16,7 ms**. Ustawienia ręczne od 2 do 32 wątków dały **15,6–62,6 ms**, dlatego pozostał tryb automatyczny. Podział rekonstrukcji 8K na osiem pasów skrócił medianę z **17,1 do 9,9 ms**. Porównanie całego bufora RGB24 potwierdziło identyczny wynik dla czterech, ośmiu, dwunastu i szesnastu pasów.

Na fragmencie pierwszych 120 klatek z NVMe graf testowy DirectShow osiągnął **21,36 kl./s** przy czterech pasach i **24,00 kl./s** przy ośmiu. Znaczniki czasu obrazu i dźwięku były identyczne, a maksymalny odstęp dostarczenia odpowiadających sobie próbek spadł z **612 do 7,7 ms**. Pomiar obejmuje testowe odbiorniki i ten fragment klipu.

## Kryterium wdrożenia

Na tym samym pliku i rendererze warto porównać czas od pakietu MXF do gotowej próbki, wliczając transfery i synchronizację. Pomiar bez zegara pokaże zapas przepustowości. W odtwarzaniu z zegarem warto sprawdzić liczbę pominiętych klatek, obciążenie CPU, przewijanie i synchronizację dźwięku. Wariant GPU ma sens, jeśli obniża obciążenie CPU lub zwiększa zapas czasu przy zachowaniu obrazu i płynności. [Zegar renderera DirectShow](https://learn.microsoft.com/en-us/windows/win32/directshow/cbaserenderer) · [Licznik klatek pominiętych przez renderer](https://learn.microsoft.com/en-us/previous-versions/ms786611(v=vs.85))
