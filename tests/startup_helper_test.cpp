#include "../src/startup.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char** argv) {
    if(argc==2 && strcmp(argv[1],"--startup-query")==0) {
        if(GetEnvironmentVariableW(L"BATTERYTRAY_TEST_HANG",nullptr,0))Sleep(30000);
        return static_cast<int>(startup::runHelper(argv[1]));
    }
    startup::State state=startup::inspect();
    if(state==startup::State::Unavailable) {printf("FAIL startup helper system query\n");return 1;}
    if(GetModuleHandleW(L"wbemprox.dll")) {printf("FAIL WMI loaded in parent process\n");return 1;}
    SetEnvironmentVariableW(L"BATTERYTRAY_TEST_HANG",L"1");
    ULONGLONG begin=GetTickCount64();
    startup::State timeout=startup::inspect();
    ULONGLONG elapsed=GetTickCount64()-begin;
    SetEnvironmentVariableW(L"BATTERYTRAY_TEST_HANG",nullptr);
    if(timeout!=startup::State::Unavailable || elapsed<7500 || elapsed>12000) {
        printf("FAIL startup helper timeout state=%lu elapsed=%llu\n",static_cast<DWORD>(timeout),elapsed);return 1;
    }
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return 1;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool orphan=false;
    if(Process32FirstW(snapshot,&entry))do {
        if(entry.th32ParentProcessID==GetCurrentProcessId() && lstrcmpiW(entry.szExeFile,L"startup_helper_test.exe")==0)orphan=true;
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    if(orphan){printf("FAIL orphan startup helper\n");return 1;}
    printf("PASS real startup helper query, unloaded WMI in parent, bounded %llums timeout and no orphan\n",elapsed);
}
