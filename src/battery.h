#pragma once
#include <stddef.h>
enum class Reply { Ignore, Success, Error };
struct Battery { int percent=-1; unsigned char status=0xff,level=0; bool exact=false; };
inline Reply match(const unsigned char* p,size_t n,unsigned char device,unsigned char feature,unsigned char command) {
    if(n<7 || (p[0]!=0x10 && p[0]!=0x11) || p[1]!=device) return Reply::Ignore;
    if((p[2]==0xff || p[2]==0x8f) && p[3]==feature && p[4]==command) return Reply::Error;
    return p[2]==feature && p[3]==command ? Reply::Success:Reply::Ignore;
}
inline bool decode(unsigned short feature,const unsigned char* p,size_t n,bool exact,Battery& out) {
    if((feature!=0x1004 && feature!=0x1000) || n<(feature==0x1004?4u:3u)) return false;
    if(exact && p[0]>100) return false;
    Battery next;
    next.exact=feature==0x1004 && exact;
    next.percent=feature==0x1000?(p[0]?p[0]:-1):(exact?p[0]:-1);
    next.level=feature==0x1004?p[1]:0;next.status=p[2];
    out=next;return true;
}
