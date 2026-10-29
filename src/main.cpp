#include "hid_device.h"
#include "icon_font.h"
#include "icon_pixels.h"
#include "refresh_policy.h"
#include "startup.h"
#include <shellapi.h>
#include <imm.h>
#include <dbt.h>
#include <stdio.h>
#include <vector>

namespace {
constexpr wchar_t ClassName[]=L"LogiBatteryTray.Native.v1";
constexpr UINT TrayMessage=WM_APP+1,Updated=WM_APP+2,StartupUpdated=WM_APP+3;
constexpr UINT Refresh=10,Startup=20,Exit=30;
HANDLE stopEvent=nullptr,refreshEvent=nullptr,workerThread=nullptr,startupThread=nullptr;
CRITICAL_SECTION stateLock;
Reading shared,current,lastGood;
HWND window=nullptr;
HICON currentIcon=nullptr;
UINT taskbarCreated=0;
bool trayAdded=false;
HDEVNOTIFY deviceNotification=nullptr;
startup::State startupState=startup::State::Unavailable;
enum class StartupOperation { Inspect,Enable,Disable };
bool startupBusy=false,startupChanging=false;
ULONGLONG lastStartupCheck=0;
HMENU visibleMenu=nullptr;
const wchar_t* startupLabel() {
    if(startupBusy)return startupChanging?L"开机启动（设置中…）":L"开机启动（检查中…）";
    return startupState==startup::State::Unavailable?L"开机启动（检查失败）":
        startupState==startup::State::WindowsDisabled?L"开机启动（Windows 已禁用）":
        startupState==startup::State::OtherPath?L"开机启动（更新程序路径）":L"开机启动";
}
UINT startupFlags() {
    return MF_STRING|(startupState==startup::State::On?MF_CHECKED:0)|(startupBusy?MF_GRAYED:0);
}
DWORD WINAPI startupWorker(void* argument) {
    auto operation=static_cast<StartupOperation>(reinterpret_cast<ULONG_PTR>(argument));
    startup::State result=operation==StartupOperation::Inspect?startup::inspect(stopEvent):
        operation==StartupOperation::Enable?startup::turnOn(stopEvent):startup::turnOff(stopEvent);
    if(WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0)
        PostMessageW(window,StartupUpdated,static_cast<WPARAM>(result),static_cast<LPARAM>(operation));
    return 0;
}
bool beginStartupOperation(StartupOperation operation) {
    if(startupBusy)return false;
    if(startupThread){CloseHandle(startupThread);startupThread=nullptr;}
    startupBusy=true;startupChanging=operation!=StartupOperation::Inspect;
    startupThread=CreateThread(nullptr,0,startupWorker,reinterpret_cast<void*>(static_cast<ULONG_PTR>(operation)),0,nullptr);
    if(!startupThread){startupBusy=false;startupChanging=false;startupState=startup::State::Unavailable;return false;}
    return true;
}
const wchar_t* chargeText(BYTE status) {
    switch(status) {
        case 0:return L"使用电池"; case 1:case 2:case 4:return L"充电中";
        case 3:return L"已充满"; case 5:case 6:case 7:return L"充电异常";
        default:return L"充电状态未知";
    }
}
const wchar_t* levelText(BYTE level) {
    switch(level) { case 1:return L"极低";case 2:return L"低";case 4:return L"良好";case 8:return L"满电";default:return L"未知"; }
}
std::wstring batteryText(const Battery& b) {
    return b.percent>=0 ? std::wstring(b.exact?L"":L"约 ")+std::to_wstring(b.percent)+L"%" : std::wstring(L"电量档位：")+levelText(b.level);
}
HICON makeIcon(const Reading& reading) {
    int size=GetSystemMetrics(SM_CXSMICON); if(size<16) size=16;
    std::wstring text=reading.online?(reading.battery.percent>=0?std::to_wstring(reading.battery.percent):L"~"):L"?";
    // A custom/translucent taskbar need not match SystemUsesLightTheme.
    // Bright text with a dark outline remains readable in either theme.
    COLORREF color=RGB(240,240,240);
    if(!reading.online) color=RGB(160,165,174);
    else if(reading.battery.status==1 || reading.battery.status==2 || reading.battery.status==4) color=RGB(90,185,255);
    else if(reading.battery.percent>=0 && reading.battery.percent<=10) color=RGB(255,95,95);
    else if(reading.battery.percent>=0 && reading.battery.percent<=20) color=RGB(255,185,65);
    BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bmi.bmiHeader.biWidth=size;
    bmi.bmiHeader.biHeight=-size;bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr; HDC dc=CreateCompatibleDC(nullptr);
    if(!dc) return nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!bitmap) {DeleteDC(dc);return nullptr;}
    HGDIOBJ old=SelectObject(dc,bitmap); memset(bits,0,size*size*4);
    HFONT font=iconFont(dc,text.c_str(),size-2);
    HGDIOBJ oldFont=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    RECT rect{0,0,size,size};DrawTextW(dc,text.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    GdiFlush();
    auto pixels=static_cast<BYTE*>(bits);
    std::vector<BYTE> coverage(size*size);
    for(int i=0;i<size*size;++i) coverage[i]=pixels[i*4];
    outlineText(coverage.data(),size,size,pixels,GetRValue(color),GetGValue(color),GetBValue(color));
    SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,old);DeleteDC(dc);
    std::vector<BYTE> maskData(((size+15)/16)*2*size,0);
    HBITMAP mask=CreateBitmap(size,size,1,1,maskData.data());
    ICONINFO ii{};ii.fIcon=TRUE;ii.hbmColor=bitmap;ii.hbmMask=mask;
    HICON icon=CreateIconIndirect(&ii);DeleteObject(bitmap);DeleteObject(mask);return icon;
}
void updateTray(bool add=false) {
    wchar_t tip[128]{};
    if(current.online) {
        swprintf(tip,128,L"%.50ls · %ls\n%ls · 更新 %02u:%02u:%02u",current.name.c_str(),batteryText(current.battery).c_str(),chargeText(current.battery.status),current.time.wHour,current.time.wMinute,current.time.wSecond);
    } else if(lastGood.online) {
        swprintf(tip,128,L"%.45ls · 暂时未响应\n旧值 %ls（%02u:%02u）\n正在自动重试，可单击立即刷新",lastGood.name.c_str(),batteryText(lastGood.battery).c_str(),lastGood.time.wHour,lastGood.time.wMinute);
    } else swprintf(tip,128,L"LogiBattery\n%.100ls",current.error.c_str());
    HICON icon=makeIcon(current); if(!icon) icon=CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));
    NOTIFYICONDATAW data{}; data.cbSize=sizeof(data);data.hWnd=window;data.uID=1;
    data.uFlags=NIF_ICON|NIF_TIP|NIF_MESSAGE|NIF_SHOWTIP;data.uCallbackMessage=TrayMessage;data.hIcon=icon;
    lstrcpynW(data.szTip,tip,128);
    bool success=Shell_NotifyIconW((add||!trayAdded)?NIM_ADD:NIM_MODIFY,&data)!=FALSE;
    if(success) {
        trayAdded=true;data.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&data);
        if(currentIcon) DestroyIcon(currentIcon); currentIcon=icon;
    } else if(icon) DestroyIcon(icon);
}
DWORD WINAPI worker(void*) {
    HANDLE events[]={stopEvent,refreshEvent};
    RefreshPolicy schedule;
    do {
        Reading next=queryBattery(stopEvent);
        if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0) break;
        EnterCriticalSection(&stateLock);shared=next;LeaveCriticalSection(&stateLock);
        PostMessageW(window,Updated,0,0);
        DWORD wait=WaitForMultipleObjects(2,events,FALSE,schedule.after(next.online));
        if(wait==WAIT_OBJECT_0 || wait==WAIT_FAILED) break;
    } while(true);
    return 0;
}
void showMenu() {
    HMENU menu=CreatePopupMenu();if(!menu) return;
    if(!startupBusy && (!lastStartupCheck || GetTickCount64()-lastStartupCheck>30000))
        beginStartupOperation(StartupOperation::Inspect);
    std::wstring status=current.online?current.name+L" · "+batteryText(current.battery):L"当前未读到电量";
    AppendMenuW(menu,MF_STRING|MF_DISABLED,0,status.c_str());AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,Refresh,L"立即刷新（默认每 5 分钟更新）");
    AppendMenuW(menu,startupFlags(),Startup,startupLabel());
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,Exit,L"退出");
    POINT cursor;GetCursorPos(&cursor);SetForegroundWindow(window);
    visibleMenu=menu;
    UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,window,nullptr);
    visibleMenu=nullptr;DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);
    if(command) PostMessageW(window,WM_COMMAND,command,0);
}
LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(taskbarCreated && message==taskbarCreated) {trayAdded=false;updateTray(true);return 0;}
    switch(message) {
        case StartupUpdated: {
            startupState=static_cast<startup::State>(wp);startupBusy=false;startupChanging=false;
            lastStartupCheck=GetTickCount64();
            if(visibleMenu){ModifyMenuW(visibleMenu,Startup,MF_BYCOMMAND|startupFlags(),Startup,startupLabel());DrawMenuBar(hwnd);}
            auto operation=static_cast<StartupOperation>(lp);
            if(operation!=StartupOperation::Inspect) {
                if(startupState==startup::State::WindowsDisabled)
                    MessageBoxW(hwnd,L"Windows 已禁用此启动项，请在系统“设置 → 应用 → 启动”中启用。",L"Battery Tray",MB_OK|MB_ICONINFORMATION);
                else if(startupState!=(operation==StartupOperation::Disable?startup::State::Off:startup::State::On))
                    MessageBoxW(hwnd,L"无法确认开机启动设置已生效。请稍后重试；程序不会将未验证的状态标记为已开启。",L"Battery Tray",MB_OK|MB_ICONERROR);
            }
            return 0;
        }
        case Updated:
            EnterCriticalSection(&stateLock);current=shared;LeaveCriticalSection(&stateLock);
            if(current.online) lastGood=current;
            updateTray();return 0;
        case TrayMessage:
            if(LOWORD(lp)==WM_RBUTTONUP || LOWORD(lp)==WM_CONTEXTMENU || LOWORD(lp)==NIN_KEYSELECT) showMenu();
            else if(LOWORD(lp)==NIN_SELECT) SetEvent(refreshEvent);
            return 0;
        case WM_COMMAND:
            if(LOWORD(wp)==Refresh) SetEvent(refreshEvent);
            else if(LOWORD(wp)==Startup) {
                if(!startupBusy && !beginStartupOperation(startupState==startup::State::On?StartupOperation::Disable:StartupOperation::Enable))
                    MessageBoxW(hwnd,L"无法启动设置检查，请稍后重试。",L"Battery Tray",MB_OK|MB_ICONERROR);
            }
            else if(LOWORD(wp)==Exit) PostMessageW(hwnd,WM_CLOSE,0,0);
            return 0;
        case WM_DEVICECHANGE:
            if(wp==DBT_DEVICEARRIVAL || wp==DBT_DEVICEREMOVECOMPLETE || wp==DBT_DEVNODES_CHANGED) SetTimer(hwnd,1,1500,nullptr);
            return TRUE;
        case WM_TIMER:KillTimer(hwnd,1);SetEvent(refreshEvent);return 0;
        case WM_SETTINGCHANGE:case WM_THEMECHANGED:updateTray();return 0;
        case WM_POWERBROADCAST:
            if(wp==PBT_APMRESUMEAUTOMATIC || wp==PBT_APMRESUMESUSPEND) SetEvent(refreshEvent);
            return TRUE;
        case WM_QUERYENDSESSION:return TRUE;
        case WM_ENDSESSION:if(wp) PostMessageW(hwnd,WM_CLOSE,0,0);return 0;
        case WM_CLOSE:
            SetEvent(stopEvent);WaitForSingleObject(workerThread,INFINITE);
            if(startupThread)WaitForSingleObject(startupThread,INFINITE);
            if(deviceNotification) {UnregisterDeviceNotification(deviceNotification);deviceNotification=nullptr;}
            {NOTIFYICONDATAW data{};data.cbSize=sizeof(data);data.hWnd=hwnd;data.uID=1;Shell_NotifyIconW(NIM_DELETE,&data);}
            if(currentIcon) {DestroyIcon(currentIcon);currentIcon=nullptr;}
            DestroyWindow(hwnd);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
int main(int argc,char** argv) {
    // This process has no text input. Opt out before any top-level window is
    // created so TSF/third-party IMEs do not load their engines into the tray.
    ImmDisableIME(static_cast<DWORD>(-1));
    if(argc==2 && (strcmp(argv[1],"--startup-query")==0 || strcmp(argv[1],"--startup-enable")==0 || strcmp(argv[1],"--startup-disable")==0))
        return static_cast<int>(startup::runHelper(argv[1]));
    if(argc>1 && (strcmp(argv[1],"--quit")==0 || strcmp(argv[1],"--refresh")==0)) {
        HWND existing=FindWindowW(ClassName,nullptr);
        if(!existing) return 3;
        return PostMessageW(existing,strcmp(argv[1],"--quit")==0?WM_CLOSE:WM_COMMAND,strcmp(argv[1],"--quit")==0?0:Refresh,0)?0:1;
    }
    bool probe=argc>1 && strcmp(argv[1],"--probe")==0;
    if(probe) {
        HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Reading r=queryBattery(stop,true);CloseHandle(stop);
        char name[512]{};WideCharToMultiByte(CP_UTF8,0,r.name.c_str(),-1,name,sizeof(name),nullptr,nullptr);
        printf("online=%s\nname=%s\npercent=%d\nexact=%s\nlevel=%u\ncharge_status=%u\nfeature=0x%04x\n",r.online?"true":"false",name,r.battery.percent,r.battery.exact?"true":"false",r.battery.level,r.battery.status,r.feature);
        return r.online?0:2;
    }
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\LogiBatteryTray.Native.v1");
    if(!mutex) return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {HWND existing=FindWindowW(ClassName,nullptr);if(existing) PostMessageW(existing,WM_COMMAND,Refresh,0);CloseHandle(mutex);return 0;}
    SetProcessDPIAware();InitializeCriticalSection(&stateLock);
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);refreshEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!stopEvent || !refreshEvent) return 1;
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=ClassName;
    if(!RegisterClassW(&wc)) return 1;
    window=CreateWindowExW(WS_EX_TOOLWINDOW,ClassName,L"LogiBattery",WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window) return 1;
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};filter.dbcc_size=sizeof(filter);filter.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;
    deviceNotification=RegisterDeviceNotificationW(window,&filter,DEVICE_NOTIFY_WINDOW_HANDLE|DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    current.error=L"正在读取鼠标电量…";updateTray(true);
    workerThread=CreateThread(nullptr,0,worker,nullptr,0,nullptr);
    beginStartupOperation(StartupOperation::Inspect);
    if(!workerThread) {MessageBoxW(nullptr,L"无法启动读取线程。",L"LogiBattery",MB_OK|MB_ICONERROR);PostMessageW(window,WM_CLOSE,0,0);}
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    if(workerThread) CloseHandle(workerThread);if(startupThread)CloseHandle(startupThread);CloseHandle(stopEvent);CloseHandle(refreshEvent);CloseHandle(mutex);DeleteCriticalSection(&stateLock);
    return 0;
}
