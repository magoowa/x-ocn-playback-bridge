#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <limits>

// One instance per streaming thread. Render failures must be propagated after GPU negotiation.
class GpuRender {
    using Int=int; using Uint=unsigned; using Bits=unsigned long long; using Props=intptr_t;
    using Platform=void*; using Device=void*; using Context=void*; using Queue=void*;
    using Mem=void*; using Program=void*; using Kernel=void*; using Event=void*;
    using GetPlatforms=Int (WINAPI*)(Uint,Platform*,Uint*);
    using GetExt=void* (WINAPI*)(Platform,const char*);
    using GetDevice=Int (WINAPI*)(Platform,Uint,void*,Uint,Uint,Device*,Uint*);
    using CreateContext=Context (WINAPI*)(const Props*,Uint,const Device*,void*,void*,Int*);
    using CreateQueue=Queue (WINAPI*)(Context,Device,Bits,Int*);
    using CreateBuffer=Mem (WINAPI*)(Context,Bits,size_t,void*,Int*);
    using CreateFromTex=Mem (WINAPI*)(Context,Bits,ID3D11Texture2D*,Uint,Int*);
    using CreateProgram=Program (WINAPI*)(Context,Uint,const char**,const size_t*,Int*);
    using BuildProgram=Int (WINAPI*)(Program,Uint,const Device*,const char*,void*,void*);
    using CreateKernel=Kernel (WINAPI*)(Program,const char*,Int*);
    using SetKernelArg=Int (WINAPI*)(Kernel,Uint,size_t,const void*);
    using WriteBuffer=Int (WINAPI*)(Queue,Mem,Uint,size_t,size_t,const void*,Uint,const Event*,Event*);
    using ObjectOp=Int (WINAPI*)(Queue,Uint,const Mem*,Uint,const Event*,Event*);
    using RunKernel=Int (WINAPI*)(Queue,Kernel,Uint,const size_t*,const size_t*,const size_t*,Uint,const Event*,Event*);
    using Finish=Int (WINAPI*)(Queue);
    using Release=Int (WINAPI*)(void*);
    HMODULE module=nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> d3d;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> heldTextures[3];
    Context context=nullptr; Queue queue=nullptr; Program program=nullptr; Kernel kernel=nullptr;
    Mem inputs[4]{}; Mem images[3]{}; unsigned nextSlot=0;
    GetPlatforms platforms=nullptr; GetExt ext=nullptr; CreateContext createContext=nullptr;
    CreateQueue createQueue=nullptr; CreateBuffer createBuffer=nullptr; CreateFromTex createFromTex=nullptr;
    CreateProgram createProgram=nullptr; BuildProgram buildProgram=nullptr; CreateKernel createKernel=nullptr;
    SetKernelArg setArg=nullptr; WriteBuffer write=nullptr; ObjectOp acquire=nullptr, releaseObjects=nullptr;
    RunKernel run=nullptr; Finish finish=nullptr;
    Release releaseMem=nullptr, releaseKernel=nullptr, releaseProgram=nullptr, releaseQueue=nullptr, releaseContext=nullptr;
    unsigned sw=0,sh=0,left=0,top=0,width=0,height=0;
    size_t sizes[3]{};
    uint8_t lastTable[65536]{}; bool tableValid=false;
    template<class T> bool Bind(T& f,const char* name) { f=reinterpret_cast<T>(GetProcAddress(module,name));return f!=nullptr; }
    static HRESULT Failed(Int code) { return code ? E_FAIL : S_OK; }
    static constexpr const char* Shader() { return R"CL(
__kernel void rgb(__global const ushort* red, __global const ushort* green,
                  __global const ushort* blue, __global const uchar* lut,
                  write_only image2d_t output, int sw, int sh, int left, int top, int outH) {
    int x=get_global_id(0), y=get_global_id(1);
    int sx=x+left, sy=y+top, hw=sw/2;
    int row=sy/2, col=sx/2, lower=sy&1, right=sx&1;
    int other=clamp(row+(lower?1:-1),0,sh/2-1);
    int side=clamp(col+(right?1:-1),0,hw-1);
    __global const ushort* r=red+row*hw; __global const ushort* b=blue+row*hw;
    __global const ushort* g=green+row*sw;
    __global const ushort* rr=red+other*hw; __global const ushort* bb=blue+other*hw;
    __global const ushort* gg=green+other*sw;
    uint rv,gv,bv;
    if (!lower&&!right) {
      rv=r[col]; gv=(g[2*side+1]+g[2*col+1]+gg[2*col]+g[2*col])/4;
      bv=(bb[side]+bb[col]+b[side]+b[col])/4;
    } else if (!lower) {
      rv=(r[col]+r[side])/2; gv=g[2*col+1]; bv=(bb[col]+b[col])/2;
    } else if (!right) {
      rv=(r[col]+rr[col])/2; gv=g[2*col]; bv=(b[side]+b[col])/2;
    } else {
      rv=(r[col]+r[side]+rr[col]+rr[side])/4;
      gv=(g[2*col]+g[2*side]+g[2*col+1]+gg[2*col+1])/4; bv=b[col];
    }
    write_imagef(output,(int2)(x,outH-1-y),(float4)(lut[rv],lut[gv],lut[bv],255)/255.0f);
})CL"; }
public:
    GpuRender()=default;
    GpuRender(const GpuRender&)=delete;
    GpuRender& operator=(const GpuRender&)=delete;
    ~GpuRender(){Reset();}
    void Reset() {
        if(queue&&finish)finish(queue);
        for(unsigned i=0;i<3;++i){if(images[i]&&releaseMem)releaseMem(images[i]);images[i]=nullptr;heldTextures[i].Reset();} nextSlot=0;
        for(auto& item:inputs){if(item&&releaseMem)releaseMem(item);item=nullptr;}
        if(kernel&&releaseKernel)releaseKernel(kernel);kernel=nullptr;
        if(program&&releaseProgram)releaseProgram(program);program=nullptr;
        if(queue&&releaseQueue)releaseQueue(queue);queue=nullptr;
        if(context&&releaseContext)releaseContext(context);context=nullptr;
        d3dContext.Reset();d3d.Reset();if(module)FreeLibrary(module);module=nullptr;
        sw=sh=left=top=width=height=0;tableValid=false;
    }
    HRESULT Init(ID3D11Device* device, ID3D11DeviceContext* immediate, unsigned storedW, unsigned storedH,
                 unsigned cropX, unsigned cropY, unsigned w, unsigned h) {
        Reset();
        if(!device||!immediate||!storedW||!storedH||(storedW&1)||(storedH&1)||!w||!h||
           uint64_t(cropX)+w>storedW||uint64_t(cropY)+h>storedH||storedW>16384||storedH>16384||
           immediate->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return E_INVALIDARG;
        Microsoft::WRL::ComPtr<ID3D11Device> contextDevice; immediate->GetDevice(&contextDevice);
        if(contextDevice.Get()!=device)return E_INVALIDARG;
        module=LoadLibraryExW(L"OpenCL.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module)return HRESULT_FROM_WIN32(GetLastError());
        if(!Bind(platforms,"clGetPlatformIDs")||!Bind(ext,"clGetExtensionFunctionAddressForPlatform")||
           !Bind(createContext,"clCreateContext")||!Bind(createQueue,"clCreateCommandQueue")||
           !Bind(createBuffer,"clCreateBuffer")||!Bind(createProgram,"clCreateProgramWithSource")||
           !Bind(buildProgram,"clBuildProgram")||!Bind(createKernel,"clCreateKernel")||!Bind(setArg,"clSetKernelArg")||
           !Bind(write,"clEnqueueWriteBuffer")||!Bind(run,"clEnqueueNDRangeKernel")||!Bind(finish,"clFinish")||
           !Bind(releaseMem,"clReleaseMemObject")||!Bind(releaseKernel,"clReleaseKernel")||
           !Bind(releaseProgram,"clReleaseProgram")||!Bind(releaseQueue,"clReleaseCommandQueue")||
           !Bind(releaseContext,"clReleaseContext")){Reset();return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);}
        try {
            Uint count=0;if(platforms(0,nullptr,&count)||!count){Reset();return E_FAIL;}
            std::vector<Platform> list(count);if(platforms(count,list.data(),nullptr)){Reset();return E_FAIL;}
            Platform selected=nullptr;Device clDevice=nullptr;
            for(auto p:list){
                auto getDevice=reinterpret_cast<GetDevice>(ext(p,"clGetDeviceIDsFromD3D11NV"));
                auto from=reinterpret_cast<CreateFromTex>(ext(p,"clCreateFromD3D11Texture2DNV"));
                auto get=reinterpret_cast<ObjectOp>(ext(p,"clEnqueueAcquireD3D11ObjectsNV"));
                auto put=reinterpret_cast<ObjectOp>(ext(p,"clEnqueueReleaseD3D11ObjectsNV"));
                if(getDevice&&from&&get&&put&&!getDevice(p,0x4019,device,0x401b,1,&clDevice,nullptr)){
                    selected=p;createFromTex=from;acquire=get;releaseObjects=put;break;
                }
            }
            if(!selected){Reset();return E_NOINTERFACE;}
            Int err=0;Props properties[]={0x1084,reinterpret_cast<Props>(selected),0x401d,reinterpret_cast<Props>(device),0};
            context=createContext(properties,1,&clDevice,nullptr,nullptr,&err);
            if(err||!context){Reset();return E_FAIL;}
            queue=createQueue(context,clDevice,0,&err);if(err||!queue){Reset();return E_FAIL;}
            const size_t pixels=size_t(storedW)*storedH;
            sizes[0]=sizes[2]=pixels/2;sizes[1]=pixels;
            for(unsigned i=0;i<4;++i){inputs[i]=createBuffer(context,4,i<3?sizes[i]:65536,nullptr,&err);if(err||!inputs[i]){Reset();return E_OUTOFMEMORY;}}
            const char* source=Shader();program=createProgram(context,1,&source,nullptr,&err);
            if(err||!program||buildProgram(program,1,&clDevice,nullptr,nullptr,nullptr)){Reset();return E_FAIL;}
            kernel=createKernel(program,"rgb",&err);if(err||!kernel){Reset();return E_FAIL;}
            for(Uint i=0;i<4;++i)if(setArg(kernel,i,sizeof(inputs[i]),&inputs[i])){Reset();return E_FAIL;}
            int values[]={int(storedW),int(storedH),int(cropX),int(cropY),int(h)};
            for(Uint i=0;i<5;++i)if(setArg(kernel,5+i,sizeof(values[i]),&values[i])){Reset();return E_FAIL;}
            d3d=device;d3dContext=immediate;sw=storedW;sh=storedH;left=cropX;top=cropY;width=w;height=h;
            return S_OK;
        } catch(const std::bad_alloc&){Reset();return E_OUTOFMEMORY;}
    }
    HRESULT Render(ID3D11Texture2D* texture,const uint16_t* plane0,const uint16_t* plane1,const uint16_t* plane2,const uint8_t* table) {
        if(!context||!queue||!texture||!plane0||!plane1||!plane2||!table)return E_INVALIDARG;
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        Microsoft::WRL::ComPtr<ID3D11Device> owner;texture->GetDevice(&owner);
        if(owner.Get()!=d3d.Get()||desc.Width!=width||desc.Height!=height||desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||desc.MipLevels!=1||desc.ArraySize!=1||desc.SampleDesc.Count!=1)return E_INVALIDARG;
        unsigned slot=0;
        while(slot<3 && texture!=heldTextures[slot].Get())++slot;
        if(slot==3){
            slot=nextSlot;nextSlot=(nextSlot+1)%3;
            if(images[slot]){releaseMem(images[slot]);images[slot]=nullptr;}heldTextures[slot].Reset();
            Int err=0;images[slot]=createFromTex(context,2,texture,0,&err);
            if(err||!images[slot])return E_FAIL;
            heldTextures[slot]=texture;
        }
        Mem image=images[slot];
        if(setArg(kernel,4,sizeof(image),&image))return E_FAIL;
        const void* planes[]={plane0,plane1,plane2};
        for(unsigned i=0;i<3;++i)if(write(queue,inputs[i],0,0,sizes[i],planes[i],0,nullptr,nullptr)){finish(queue);return E_FAIL;}
        if(!tableValid||memcmp(lastTable,table,sizeof(lastTable))){
            if(write(queue,inputs[3],1,0,sizeof(lastTable),table,0,nullptr,nullptr)){finish(queue);return E_FAIL;}
            memcpy(lastTable,table,sizeof(lastTable));tableValid=true;
        }
        if(acquire(queue,1,&image,0,nullptr,nullptr)){finish(queue);return E_FAIL;}
        size_t global[]={width,height};
        const Int kernelResult=run(queue,kernel,2,nullptr,global,nullptr,0,nullptr,nullptr);
        const Int releaseResult=releaseObjects(queue,1,&image,0,nullptr,nullptr);
        const Int finishResult=finish(queue);
        if(kernelResult||releaseResult||finishResult){Reset();return E_FAIL;}
        return S_OK;
    }
};
