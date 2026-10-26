#include "../src/icon_pixels.h"
#include <stdio.h>
#define CHECK(x) do{if(!(x)){printf("FAIL contrast line %d: %s\n",__LINE__,#x);return 1;}++count;}while(0)
int main() {
    int count=0;unsigned char mask[25]{};unsigned char pixels[100]{};mask[12]=255;
    outlineText(mask,5,5,pixels,240,240,240);
    CHECK(pixels[12*4]==240 && pixels[12*4+3]==255);
    CHECK(pixels[11*4]==20 && pixels[11*4+3]==255);
    CHECK(pixels[0*4+3]==0);
    CHECK(pixels[6*4]==20 && pixels[6*4+3]==255);
    // Both light foreground and dark outline must survive compositing against
    // either taskbar theme and the user's gray hover background.
    const int backgrounds[]={35,145,240};
    for(int bg:backgrounds) {
        int center=pixels[48]+bg*(255-pixels[51])/255;
        int edge=pixels[44]+bg*(255-pixels[47])/255;
        CHECK(center-edge>=200);
    }
    mask[12]=128;outlineText(mask,5,5,pixels,90,185,255);
    CHECK(pixels[51]>=128 && pixels[51]<255);
    for(int i=0;i<25;++i) for(int channel=0;channel<3;++channel) CHECK(pixels[i*4+channel]<=pixels[i*4+3]);
    printf("PASS %d outlined icon pixel checks\n",count);
}
