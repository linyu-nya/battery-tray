// A private test key is used; this never toggles the real startup entry.
#include "../src/startup.cpp"
#include <stdio.h>
int main(int argc,char** argv) {
    std::wstring key=L"Software\\BatteryTray\\StartupTests\\"+std::to_wstring(GetCurrentProcessId());
    if(argc==2) {
        key=std::wstring(argv[1],argv[1]+strlen(argv[1]));
        if(key.find(L"Software\\BatteryTray\\StartupTests\\")!=0)return 1;
    }
    const std::wstring approval=key+L"\\Approved";
    startup::WmiRegistry registry;
    startup::ComPtr<IWbemClassObject> created,approved;
    DWORD code=0;
    if(!registry.call(L"CreateKey",key,nullptr,nullptr,created,code) || code!=0 ||
       !registry.call(L"CreateKey",approval,nullptr,nullptr,approved,code) || code!=0) {
        printf("FAIL WMI test fixture creation\n");return 1;
    }
    startup::WmiStore store(key,L"TestValue",approval);
    const std::wstring command=L"\"D:\\BatteryTray Test\\BatteryTray.exe\"";
    bool ok=startup::query(store,command)==startup::State::Off;
    ok=ok && startup::enable(store,command)==startup::State::On;
    ok=ok && startup::query(store,command)==startup::State::On;
    ok=ok && startup::query(store,L"\"D:\\Other\\BatteryTray.exe\"")==startup::State::OtherPath;
    ok=ok && startup::disable(store,command)==startup::State::Off;
    ok=ok && startup::disable(store,command)==startup::State::Off;
    if(argc==2) {
        const wchar_t* names[]={L"Enabled2",L"Disabled3",L"Enabled6",L"Disabled7",L"Malformed"};
        for(int i=0;i<5;++i) {
            startup::WmiStore fixture(key,names[i],approval);
            startup::State expected=i==4?startup::State::Unavailable:
                (i==1 || i==3?startup::State::WindowsDisabled:startup::State::On);
            ok=startup::enable(fixture,command)==expected && ok;
            ok=startup::query(fixture,command)==expected && ok;
            ok=fixture.remove() && ok;
        }
    }
    // Always remove the test value and then only this test's private keys.
    bool clean=store.remove();
    startup::ComPtr<IWbemClassObject> removedApproval,removed;
    if(argc!=2) {
        clean=registry.call(L"DeleteKey",approval,nullptr,nullptr,removedApproval,code) && code==0 && clean;
        clean=registry.call(L"DeleteKey",key,nullptr,nullptr,removed,code) && code==0 && clean;
    }
    if(!ok || !clean){printf("FAIL WMI startup round-trip or cleanup\n");return 1;}
    printf("PASS Windows WMI startup round-trip, path verification, approval binary states and cleanup\n");
    return 0;
}
