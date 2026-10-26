#define main trayApplicationMain
#include "../src/main.cpp"
#undef main

int main() {
    SetProcessDPIAware();
    const int width=320,height=144;
    std::vector<BYTE> canvas(width*height*4,255);
    const int backgrounds[]={40,145,240};
    const int values[]={9,42,91,100,91};
    int size=GetSystemMetrics(SM_CXSMICON);
    for(int row=0;row<3;++row) {
        for(int y=row*48;y<(row+1)*48;++y) for(int x=0;x<width;++x) for(int c=0;c<3;++c) canvas[(y*width+x)*4+c]=static_cast<BYTE>(backgrounds[row]);
        for(int col=0;col<5;++col) {
            Reading reading;reading.online=true;reading.battery.percent=values[col];reading.battery.exact=true;reading.battery.status=col==4?1:0;
            HICON icon=makeIcon(reading);ICONINFO info{};
            if(!icon || !GetIconInfo(icon,&info)) return 1;
            BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bmi.bmiHeader.biWidth=size;bmi.bmiHeader.biHeight=-size;
            bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
            std::vector<BYTE> pixels(size*size*4);HDC dc=GetDC(nullptr);
            int got=GetDIBits(dc,info.hbmColor,0,size,pixels.data(),&bmi,DIB_RGB_COLORS);ReleaseDC(nullptr,dc);
            DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);DestroyIcon(icon);if(!got) return 2;
            for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
                int dest=((row*48+(48-size)/2+y)*width+col*64+(64-size)/2+x)*4;
                int source=(y*size+x)*4;
                for(int c=0;c<3;++c) canvas[dest+c]=static_cast<BYTE>(pixels[source+c]+backgrounds[row]*(255-pixels[source+3])/255);
            }
        }
    }
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+static_cast<DWORD>(canvas.size());
    BITMAPINFOHEADER bmi{};bmi.biSize=sizeof(bmi);bmi.biWidth=width;bmi.biHeight=-height;bmi.biPlanes=1;bmi.biBitCount=32;bmi.biCompression=BI_RGB;
    HANDLE file=CreateFileW(L"build\\icon-preview.bmp",GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);if(file==INVALID_HANDLE_VALUE) return 3;
    DWORD done=0;WriteFile(file,&header,sizeof(header),&done,nullptr);WriteFile(file,&bmi,sizeof(bmi),&done,nullptr);WriteFile(file,canvas.data(),static_cast<DWORD>(canvas.size()),&done,nullptr);CloseHandle(file);
    printf("Rendered actual tray icons, size %d, on dark/hover/light backgrounds\n",size);
    return 0;
}
