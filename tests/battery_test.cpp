#include "../src/battery.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } ++count; } while(0)
int main() {
    int count = 0;
    unsigned char p[] = {0x11,1,7,0x1d,78,4,0,0};
    CHECK(match(p,8,1,7,0x1d) == Reply::Success);
    CHECK(match(p,8,2,7,0x1d) == Reply::Ignore);
    CHECK(match(p,8,1,8,0x1d) == Reply::Ignore);
    CHECK(match(p,8,1,7,0x1c) == Reply::Ignore);
    CHECK(match(p,3,1,7,0x1d) == Reply::Ignore);
    unsigned char error[] = {0x11,1,0xff,7,0x1d,2,0};
    CHECK(match(error,7,1,7,0x1d) == Reply::Error);
    unsigned char oldError[] = {0x10,1,0x8f,7,0x1d,1,0};
    CHECK(match(oldError,7,1,7,0x1d) == Reply::Error);
    CHECK(match(error,7,1,7,0x1e) == Reply::Ignore);
    Battery b{};
    CHECK(decode(0x1004,p+4,4,true,b) && b.percent==78 && b.status==0 && b.exact);
    p[4]=0;
    CHECK(decode(0x1004,p+4,4,true,b) && b.percent==0 && b.exact);
    CHECK(decode(0x1004,p+4,4,false,b) && b.percent==-1 && b.level==4 && !b.exact);
    p[4]=101;
    CHECK(!decode(0x1004,p+4,4,true,b));
    CHECK(!decode(0x1004,p+4,2,true,b));
    unsigned char legacy[] = {100,20,3};
    CHECK(decode(0x1000,legacy,3,true,b) && b.percent==100 && b.status==3 && !b.exact);
    legacy[0]=0;
    CHECK(decode(0x1000,legacy,3,true,b) && b.percent==-1 && !b.exact);
    CHECK(!decode(0x1234,legacy,3,true,b));
    printf("PASS %d protocol checks\n",count);
}
