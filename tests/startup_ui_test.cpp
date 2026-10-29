#define main trayApplicationMain
#include "../src/main.cpp"
#undef main
#include <stdio.h>
bool echoReceived=false;
LRESULT CALLBACK testWindowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_APP+99){echoReceived=true;return 0;}
    return windowProc(hwnd,message,wp,lp);
}
int main(int argc,char** argv) {
    if(argc==2 && strcmp(argv[1],"--startup-query")==0) {
        if(GetEnvironmentVariableW(L"BATTERYTRAY_TEST_HANG",nullptr,0)) {
            wchar_t eventName[256]{};
            if(GetEnvironmentVariableW(L"BATTERYTRAY_TEST_READY",eventName,256)) {
                HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName);
                if(ready){SetEvent(ready);CloseHandle(ready);}
            }
            Sleep(30000);
        }
        return static_cast<int>(startup::runHelper(argv[1]));
    }
    stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stopEvent)return 1;
    WNDCLASSW wc{};wc.lpfnWndProc=testWindowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"BatteryTray.StartupUITest";
    if(!RegisterClassW(&wc))return 1;
    window=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
    if(!window)return 1;
    std::wstring eventName=L"Local\\BatteryTray.StartupUITest.Ready."+std::to_wstring(GetCurrentProcessId());
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());
    if(!ready)return 1;
    SetEnvironmentVariableW(L"BATTERYTRAY_TEST_HANG",L"1");
    SetEnvironmentVariableW(L"BATTERYTRAY_TEST_READY",eventName.c_str());
    ULONGLONG begin=GetTickCount64();
    if(!beginStartupOperation(StartupOperation::Inspect))return 1;
    while(WaitForSingleObject(ready,0)!=WAIT_OBJECT_0 && GetTickCount64()-begin<5000) {
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        Sleep(1);
    }
    if(WaitForSingleObject(ready,0)!=WAIT_OBJECT_0){SetEvent(stopEvent);WaitForSingleObject(startupThread,2000);printf("FAIL helper never entered hung operation\n");return 1;}
    ULONGLONG responseBegin=GetTickCount64();
    PostMessageW(window,WM_APP+99,0,0);
    while(!echoReceived && GetTickCount64()-responseBegin<500) {
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        if(!echoReceived)Sleep(1);
    }
    bool responsive=echoReceived && startupBusy && GetTickCount64()-responseBegin<500;
    ULONGLONG cancelBegin=GetTickCount64();SetEvent(stopEvent);
    bool cancelled=WaitForSingleObject(startupThread,2000)==WAIT_OBJECT_0;
    ULONGLONG elapsed=GetTickCount64()-cancelBegin;
    SetEnvironmentVariableW(L"BATTERYTRAY_TEST_HANG",nullptr);
    SetEnvironmentVariableW(L"BATTERYTRAY_TEST_READY",nullptr);CloseHandle(ready);
    if(!cancelled){printf("FAIL startup cancellation\n");return 1;}
    CloseHandle(startupThread);startupThread=nullptr;
    if(!responsive){printf("FAIL message loop blocked by startup helper\n");return 1;}
    // A completed async inspection must also update an already-open menu.
    ResetEvent(stopEvent);startupBusy=false;startupChanging=false;
    if(!beginStartupOperation(StartupOperation::Inspect))return 1;
    visibleMenu=CreatePopupMenu();AppendMenuW(visibleMenu,startupFlags(),Startup,startupLabel());
    begin=GetTickCount64();
    while(startupBusy && GetTickCount64()-begin<9000) {
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        Sleep(1);
    }
    bool menuReady=!startupBusy && startupState!=startup::State::Unavailable &&
        !(GetMenuState(visibleMenu,Startup,MF_BYCOMMAND)&(MF_GRAYED|MF_DISABLED));
    SetEvent(stopEvent);WaitForSingleObject(startupThread,2000);CloseHandle(startupThread);startupThread=nullptr;
    DestroyMenu(visibleMenu);visibleMenu=nullptr;
    DestroyWindow(window);CloseHandle(stopEvent);
    if(!menuReady){printf("FAIL async menu state refresh\n");return 1;}
    printf("PASS tray responsive with ready hung helper; cancellation %llums; open menu state refreshed\n",elapsed);
}
