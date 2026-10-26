#pragma once
#include <windows.h>
inline HFONT iconFont(HDC dc,const wchar_t* text,int size) {
    for(int height=size;height>0;--height) {
        HFONT font=CreateFontW(-height,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!font) return nullptr;
        HGDIOBJ old=SelectObject(dc,font);SIZE extent{};
        BOOL measured=GetTextExtentPoint32W(dc,text,lstrlenW(text),&extent);
        SelectObject(dc,old);
        if(measured && extent.cx<=size-1 && extent.cy<=size) return font;
        DeleteObject(font);
    }
    return nullptr;
}
