#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
class GpuReadback {
using I=int; using U=unsigned; using B=unsigned long long; using P=void*; using S=size_t;
#define F(name,ret,...) using name##T=ret (WINAPI*)(__VA_ARGS__); name##T name=nullptr
F(platforms,I,U,P*,U*); F(devices,I,P,B,U,P*,U*); F(createContext,P,const intptr_t*,U,const P*,P,P,I*);
F(createQueue,P,P,P,B,I*); F(createBuffer,P,P,B,S,P,I*); F(createProgram,P,P,U,const char**,const S*,I*);
F(build,I,P,U,const P*,const char*,P,P); F(createKernel,P,P,const char*,I*); F(set,I,P,U,S,const void*);
F(write,I,P,P,U,S,S,const void*,U,const P*,P*); F(read,I,P,P,U,S,S,P,U,const P*,P*);
F(run,I,P,P,U,const S*,const S*,const S*,U,const P*,P*); F(finish,I,P);
F(extension,P,P,const char*);
F(d3dDevice,I,P,U,P,U,U,P*,U*);
F(fromTexture,P,P,B,P,U,I*);
F(acquire,I,P,U,const P*,U,const P*,P*);
F(release,I,P,U,const P*,U,const P*,P*);
F(releaseMem,I,P); F(releaseKernel,I,P); F(releaseProgram,I,P); F(releaseQueue,I,P); F(releaseContext,I,P);
#undef F
HMODULE dll=nullptr; P selectedDevice=nullptr,ctx=nullptr,q=nullptr,program=nullptr,kernel=nullptr,buf[5]{};
U textureArg=2;
S sizes[3]{},outSize=0; unsigned width=0,height=0; unsigned char table[65536]{};bool valid=false;
template<class T>bool bind(T& p,const char* n){p=reinterpret_cast<T>(GetProcAddress(dll,n));return p!=nullptr;}
public:
static const wchar_t* SystemStatus() {
    GpuReadback probe;
    probe.dll = LoadLibraryExW(L"OpenCL.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!probe.dll) return L"System loader unavailable";
    if (!probe.bind(probe.platforms, "clGetPlatformIDs") ||
        !probe.bind(probe.devices, "clGetDeviceIDs")) return L"Loader API unavailable";
    U count = 0;
    if (probe.platforms(0, nullptr, &count) || !count) return L"No platform found";
    try {
        std::vector<P> list(count);
        if (probe.platforms(count, list.data(), nullptr)) return L"Platform query failed";
        for (auto platform : list)
            if (!probe.devices(platform, 4, 0, nullptr, &count) && count) return L"GPU detected";
    } catch (const std::bad_alloc&) {
        return L"GPU query failed";
    }
    return L"No GPU detected";
}
~GpuReadback(){reset();}
void reset(){if(q&&finish)finish(q);for(auto& b:buf)if(b&&releaseMem){releaseMem(b);b=nullptr;}if(kernel&&releaseKernel)releaseKernel(kernel);kernel=nullptr;if(program&&releaseProgram)releaseProgram(program);program=nullptr;if(q&&releaseQueue)releaseQueue(q);q=nullptr;if(ctx&&releaseContext)releaseContext(ctx);ctx=nullptr;if(dll)FreeLibrary(dll);dll=nullptr;valid=false;}
bool init(unsigned sw,unsigned sh,unsigned x,unsigned y,unsigned w,unsigned h,unsigned sonyScale, void* d3d=nullptr){
const bool xtPreview = sonyScale != 0;
reset();if(!sw||!sh||!w||!h||x+w>sw||y+h>sh)return false;dll=LoadLibraryExW(L"OpenCL.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!dll)return false;
#define BIND(a,b) if(!bind(a,b)){reset();return false;}
BIND(platforms,"clGetPlatformIDs");BIND(devices,"clGetDeviceIDs");BIND(createContext,"clCreateContext");BIND(createQueue,"clCreateCommandQueue");BIND(createBuffer,"clCreateBuffer");BIND(createProgram,"clCreateProgramWithSource");BIND(build,"clBuildProgram");BIND(createKernel,"clCreateKernel");BIND(set,"clSetKernelArg");BIND(write,"clEnqueueWriteBuffer");BIND(read,"clEnqueueReadBuffer");BIND(run,"clEnqueueNDRangeKernel");BIND(finish,"clFinish");BIND(releaseMem,"clReleaseMemObject");BIND(releaseKernel,"clReleaseKernel");BIND(releaseProgram,"clReleaseProgram");BIND(releaseQueue,"clReleaseCommandQueue");BIND(releaseContext,"clReleaseContext");
#undef BIND
U count=0;if(platforms(0,nullptr,&count)||!count){reset();return false;}std::vector<P> ps(count);if(platforms(count,ps.data(),nullptr)){reset();return false;}P device=nullptr;
intptr_t properties[5]{};
if (d3d) {
    if (!bind(extension,"clGetExtensionFunctionAddressForPlatform")) { reset(); return false; }
    for (auto platform : ps) {
        d3dDevice=reinterpret_cast<d3dDeviceT>(extension(platform,"clGetDeviceIDsFromD3D11NV"));
        fromTexture=reinterpret_cast<fromTextureT>(extension(platform,"clCreateFromD3D11Texture2DNV"));
        acquire=reinterpret_cast<acquireT>(extension(platform,"clEnqueueAcquireD3D11ObjectsNV"));
        release=reinterpret_cast<releaseT>(extension(platform,"clEnqueueReleaseD3D11ObjectsNV"));
        if (d3dDevice && fromTexture && acquire && release &&
            !d3dDevice(platform,0x4019,d3d,0x401b,1,&device,nullptr)) {
            properties[0]=0x1084; properties[1]=reinterpret_cast<intptr_t>(platform);
            properties[2]=0x401d; properties[3]=reinterpret_cast<intptr_t>(d3d);
            break;
        }
    }
    if (!properties[0]) { reset(); return false; }
} else {
    for (auto platform : ps) if (!devices(platform,4,1,&device,nullptr)) break;
}
if(!device){reset();return false;}I err=0;ctx=createContext(d3d?properties:nullptr,1,&device,nullptr,nullptr,&err);if(err||!ctx){reset();return false;}q=createQueue(ctx,device,0,&err);if(err||!q){reset();return false;}
if(sonyScale){w/=sonyScale;h/=sonyScale;} S pixels=S(sw)*sh;sizes[0]=sizes[2]=pixels/2;sizes[1]=pixels;unsigned stride=w*4;outSize=S(stride)*h;
for(int i=0;i<5;i++){if((xtPreview&&(i==1||i==2))||(d3d&&i==4))continue; S len=i==0&&xtPreview?outSize*2:(i<3?sizes[i]:(i==3?65536:outSize));buf[i]=createBuffer(ctx,1,len,nullptr,&err);if(err||!buf[i]){reset();return false;}}
const char* src=d3d&&xtPreview?R"CL(
__kernel void preview(__global const ushort4* rgba, __global const uchar* lut,
                      write_only image2d_t output, int width, int height) {
    const int x=get_global_id(0), y=get_global_id(1);
    const ushort4 pixel=rgba[y*width+x];
    write_imagef(output,(int2)(x,y),(float4)(lut[pixel.x],lut[pixel.y],lut[pixel.z],255)/255.0f);
})CL":xtPreview?R"CL(
__kernel void preview(__global const ushort4* rgba, __global const uchar* lut,
                      __global uchar4* output, int width, int height) {
    const int x=get_global_id(0), y=get_global_id(1);
    const ushort4 pixel=rgba[y*width+x];
    output[(height-1-y)*width+x]=(uchar4)(lut[pixel.z],lut[pixel.y],lut[pixel.x],255);
})CL":R"CL(
__kernel void rgb(__global const ushort* red, __global const ushort* green,
                  __global const ushort* blue, __global const uchar* lut,
                  #ifdef TEXTURE_OUTPUT
                  write_only image2d_t output,
#else
                  __global uchar4* output,
#endif
                  int sw, int sh, int left, int top, int outH, int outW, int stride) {
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
#ifdef TEXTURE_OUTPUT
    write_imagef(output,(int2)(x,y),(float4)(lut[rv],lut[gv],lut[bv],255)/255.0f);
#else
    output[(size_t)(outH-1-y)*outW+x]=(uchar4)(lut[bv],lut[gv],lut[rv],255);
#endif
})CL";program=createProgram(ctx,1,&src,nullptr,&err);if(err||!program||build(program,1,&device,d3d&&!xtPreview?"-DTEXTURE_OUTPUT":nullptr,nullptr,nullptr)){reset();return false;}kernel=createKernel(program,xtPreview?"preview":"rgb",&err);if(err||!kernel){reset();return false;}textureArg=xtPreview?2:4;
if(xtPreview){
    const int dimensions[]={int(w),int(h)};
    if(set(kernel,0,sizeof(P),&buf[0])||set(kernel,1,sizeof(P),&buf[3])||
       (!d3d&&set(kernel,2,sizeof(P),&buf[4]))||set(kernel,3,sizeof(int),&dimensions[0])||
       set(kernel,4,sizeof(int),&dimensions[1])){reset();return false;}
    width=w;height=h;selectedDevice=device;return true;
}
for(U i=0;i<(d3d?4u:5u);i++)if(set(kernel,i,sizeof(P),&buf[i])){reset();return false;}int values[]={int(sw),int(sh),int(x),int(y),int(h),int(w),int(stride)};for(U i=0;i<7;i++)if(set(kernel,5+i,sizeof(int),&values[i])){reset();return false;}width=w;height=h;selectedDevice=device;return true;}
bool startDecoder(Decoder& decoder,unsigned sonyScale){return decoder.StartGpu(selectedDevice,ctx,buf,sonyScale);}
bool renderTexture(void* texture,const uint8_t* lut) {
    if (!valid || memcmp(table,lut,sizeof(table))) {
        if(write(q,buf[3],1,0,sizeof(table),lut,0,nullptr,nullptr)) return false;
        memcpy(table,lut,sizeof(table)); valid=true;
    }
    I error=0;
    P image=fromTexture(ctx,2,texture,0,&error);
    if(error||!image) return false;
    const bool acquired=!acquire(q,1,&image,0,nullptr,nullptr);
    S dims[]={width,height};
    const bool rendered=acquired && !set(kernel,textureArg,sizeof(P),&image) &&
        !run(q,kernel,2,nullptr,dims,nullptr,0,nullptr,nullptr);
    const bool released=acquired && !release(q,1,&image,0,nullptr,nullptr);
    const bool completed=!finish(q);
    releaseMem(image);
    return rendered && released && completed;
}
bool render(unsigned char* output,const uint8_t* lut){if(!q)return false;if(!valid||memcmp(table,lut,sizeof(table))){if(write(q,buf[3],1,0,sizeof(table),lut,0,nullptr,nullptr)){finish(q);return false;}memcpy(table,lut,sizeof(table));valid=true;}S dims[]={width,height};if(run(q,kernel,2,nullptr,dims,nullptr,0,nullptr,nullptr)||read(q,buf[4],1,0,outSize,output,0,nullptr,nullptr)){finish(q);return false;}return true;}
};
