// XOCN-MPC by magoowa, version 1.5.2026.1114 (2026-11-14)
#define NOMINMAX
#include <windows.h>
#include <dshow.h>
#include <cstdio>
#include "../filter/decode.h"
int wmain(int argc,wchar_t** argv) {
    if (argc != 2) return 2;
    auto input = Open(argv[1]);
    Clip clip;
    if (!input || !clip.Load(input.get())) return 1;
    Bytes packet;
    if (!Read(input.get(), clip.video.front(), packet) || packet.size() < 64 || BE(packet.data(),3) != 0x534252) return 1;
    unsigned w=clip.cropW ? clip.cropW : BE(&packet[8],2);
    unsigned h=clip.cropH ? clip.cropH : 2 * BE(&packet[10],2);
    printf("%u %u %u %u %u %u\n",w,h,clip.aspectX,clip.aspectY,clip.fpsNum,clip.fpsDen);
    return 0;
}
