#include "../src/icon_font.h"
#include <stdio.h>
int main() {
    HDC dc=CreateCompatibleDC(nullptr);int count=0;
    const int sizes[]={16,20,24,32,48};
    for(int size:sizes) for(int value=0;value<=100;++value) {
        wchar_t text[8];swprintf(text,8,L"%d",value);
        HFONT font=iconFont(dc,text,size);HGDIOBJ old=SelectObject(dc,font);
        SIZE extent{};GetTextExtentPoint32W(dc,text,lstrlenW(text),&extent);
        SelectObject(dc,old);DeleteObject(font);
        if(extent.cx>size-1 || extent.cy>size) {printf("FAIL icon=%d value=%d width=%ld height=%ld\n",size,value,extent.cx,extent.cy);DeleteDC(dc);return 1;}
        ++count;
    }
    DeleteDC(dc);printf("PASS %d icon font fit checks\n",count);
}
