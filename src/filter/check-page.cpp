#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;
const CLSID id{0x927eb97c,0xda96,0x4af7,{0x9d,0x70,0xf3,0x14,0x32,0x67,0x48,0x62}};
struct __declspec(uuid("D482AB74-5C97-4D6B-9CBB-22A8E9414930")) IXocnSettings : IUnknown {virtual HRESULT STDMETHODCALLTYPE GetInfo(unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,unsigned*,unsigned*)=0; virtual HRESULT STDMETHODCALLTYPE SetSqueeze(unsigned)=0; virtual HRESULT STDMETHODCALLTYPE GetMetadata(BSTR*,unsigned*,unsigned*,unsigned*,BOOL*,ULONGLONG*,LONGLONG*)=0; virtual HRESULT STDMETHODCALLTYPE SetDisplayLut(BOOL)=0;};
struct __declspec(uuid("03481710-D73E-4674-839F-03EDE2D60ED8")) ISpecifyPropertyPages2 : ISpecifyPropertyPages { virtual HRESULT STDMETHODCALLTYPE CreatePage(const GUID&,IPropertyPage**)=0; };
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 auto mod=LoadLibraryW(argv[1]); if(!mod){printf("LoadLibrary %lu\n",GetLastError());return 3;}
 auto fn=(HRESULT(WINAPI*)(REFCLSID,REFIID,void**))GetProcAddress(mod,"DllGetClassObject");
 ComPtr<IClassFactory> factory; HRESULT hr=fn(id,IID_PPV_ARGS(&factory)); printf("factory %08lx\n",hr); if(FAILED(hr))return 4;
 ComPtr<IBaseFilter> filter; hr=factory->CreateInstance(nullptr,IID_PPV_ARGS(&filter));printf("create %08lx\n",hr);if(FAILED(hr))return 5;
 ComPtr<IFileSourceFilter> file; filter.As(&file); hr=file->Load(argv[2],nullptr);printf("load %08lx\n",hr);if(FAILED(hr))return 6;
 ComPtr<IXocnSettings> settings;hr=filter.As(&settings);printf("settings %08lx\n",hr);if(FAILED(hr))return 7;
 unsigned w,h,mx,my,ax,ay,n,d;hr=settings->GetInfo(&w,&h,&mx,&my,&ax,&ay,&n,&d);printf("raster %ux%u MXF %u:%u output %u:%u fps %u/%u\n",w,h,mx,my,ax,ay,n,d);
 BSTR name=nullptr; unsigned channels=0,hz=0,bits=0; BOOL isFloat=FALSE; ULONGLONG frames=0; LONGLONG duration=0;
 hr=settings->GetMetadata(&name,&channels,&hz,&bits,&isFloat,&frames,&duration);
 if(FAILED(hr)||!name||frames==0||duration<=0)return 16;
 printf("metadata frames %llu audio %u channels %u Hz %u bits\n",frames,channels,hz,bits); SysFreeString(name);
 ComPtr<IEnumPins> pins; filter->EnumPins(&pins); ComPtr<IPin> pin; pins->Next(1,&pin,nullptr);
 ComPtr<IEnumMediaTypes> types; pin->EnumMediaTypes(&types); AM_MEDIA_TYPE* mt=nullptr; hr=types->Next(1,&mt,nullptr);
 if(FAILED(hr)||!mt||mt->formattype!=FORMAT_VideoInfo2)return 14;
 auto vi=reinterpret_cast<VIDEOINFOHEADER2*>(mt->pbFormat);
 printf("media type %ldx%ld aspect %lu:%lu\n",vi->bmiHeader.biWidth,vi->bmiHeader.biHeight,vi->dwPictAspectRatioX,vi->dwPictAspectRatioY);
 if(vi->dwPictAspectRatioX!=ax||vi->dwPictAspectRatioY!=ay)return 15;
 CoTaskMemFree(mt->pbFormat); if(mt->pUnk)mt->pUnk->Release();CoTaskMemFree(mt);
 ComPtr<ISpecifyPropertyPages> pages;hr=filter.As(&pages);printf("pages %08lx\n",hr);if(FAILED(hr))return 8;
 CAUUID ids{};hr=pages->GetPages(&ids); printf("GetPages %08lx count %lu\n",hr,ids.cElems);
 ComPtr<ISpecifyPropertyPages2> direct; hr=filter.As(&direct); if(FAILED(hr))return 17;
 ComPtr<IPropertyPage> directPage; hr=direct->CreatePage(ids.pElems[0],&directPage); printf("direct page %08lx\n",hr); if(FAILED(hr))return 18;
 IUnknown* directObjects[]={filter.Get()}; hr=directPage->SetObjects(1,directObjects); printf("direct page connect %08lx\n",hr); if(FAILED(hr))return 19; directPage->SetObjects(0,nullptr);
 ComPtr<IPropertyPage> registered; HRESULT reg=CoCreateInstance(ids.pElems[0],nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&registered)); printf("registered page %08lx\n",reg); if (FAILED(reg)) return 13;
 ComPtr<IClassFactory> pageFactory;hr=fn(ids.pElems[0],IID_PPV_ARGS(&pageFactory)); printf("page factory %08lx\n",hr); CoTaskMemFree(ids.pElems); if(FAILED(hr))return 9;
 ComPtr<IPropertyPage> page;hr=pageFactory->CreateInstance(nullptr,IID_PPV_ARGS(&page));printf("page create %08lx\n",hr);if(FAILED(hr))return 10;
 IUnknown* objects[]={filter.Get()};hr=page->SetObjects(1,objects);printf("page connect %08lx\n",hr);if(FAILED(hr))return 11;
 PROPPAGEINFO info{}; hr=page->GetPageInfo(&info);printf("page info %08lx size %ldx%ld\n",hr,info.size.cx,info.size.cy);CoTaskMemFree(info.pszTitle);page->SetObjects(0,nullptr);return FAILED(hr)?12:0;
}
