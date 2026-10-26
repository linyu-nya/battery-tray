#include "../src/refresh_policy.h"
#include <stdio.h>
#define CHECK(x) do {if(!(x)){printf("FAIL recovery line %d: %s\n",__LINE__,#x);return 1;}++count;}while(0)
int main() {
    int count=0;RefreshPolicy schedule;
    CHECK(schedule.after(true)==300000);
    CHECK(schedule.after(false)==5000);
    CHECK(schedule.after(false)==15000);
    CHECK(schedule.after(false)==60000);
    for(int i=0;i<20;++i) CHECK(schedule.after(false)==60000);
    CHECK(schedule.after(true)==300000);
    CHECK(schedule.after(false)==5000);
    printf("PASS %d recovery scheduling checks\n",count);
}
