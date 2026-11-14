#define NOMINMAX
#include <dshow.h>
#include "decode.h"
#include <cstdio>
int wmain(int argc,wchar_t** argv) {
 if(argc != 2) return 2;
 if(SLog3Code(512,512,1504)!=24 || SLog3Code(1504,512,1504)!=105 ||
    SLog3Code(5472,512,1504)!=149 || SLog3Code(46554,512,1504)<=SLog3Code(16896,512,1504) ||
    SLog3Code(65535,512,1504)>=255) return 6;
 auto file=Open(argv[1]); Clip clip; Bytes frame;
 if(!file || !clip.Load(file.get()) || !Read(file.get(),clip.video[clip.video.size()/5],frame)) return 3;
 if(clip.rawBlack!=512 || clip.rawGray!=1504) { printf("raw points %u %u\n",clip.rawBlack,clip.rawGray); return 7; }
 Decoder dec;
 if(FAILED(dec.Init(frame,clip,false,true))) return 4;
 Bytes a(dec.Size()),b(dec.Size()),c(dec.Size());
 dec.Render(a.data()); dec.SetDisplayLut(true); dec.Render(b.data()); dec.SetDisplayLut(false); dec.Render(c.data());
 size_t changed=0; for(size_t i=0;i<a.size();++i) changed+=a[i]!=b[i];
 printf("%ux%u raw black=%u gray=%u changed=%zu restored=%s\n",dec.width,dec.height,clip.rawBlack,clip.rawGray,changed,a==c?"yes":"no");
 return changed > 1000 && a == c ? 0 : 5;
}
