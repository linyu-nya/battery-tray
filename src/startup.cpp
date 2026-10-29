#include "startup.h"
#include <wbemidl.h>
#include <vector>
#include <string.h>

namespace startup {
namespace {
constexpr wchar_t RunPath[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t ApprovalPath[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
// Preserve the original value name so upgrades update the existing entry.
constexpr wchar_t ValueName[]=L"LogiBatteryTray";
template<class T> struct ComPtr {
    T* p=nullptr;
    ~ComPtr(){if(p)p->Release();}
    T* operator->()const{return p;}
    T** out(){return &p;}
};
struct Runtime {
    HMODULE ole=nullptr,automation=nullptr;
    decltype(&CoInitializeEx) init=nullptr;
    decltype(&CoInitializeSecurity) security=nullptr;
    decltype(&CoUninitialize) uninit=nullptr;
    decltype(&CoCreateInstance) create=nullptr;
    decltype(&CoSetProxyBlanket) blanket=nullptr;
    decltype(&SysAllocString) alloc=nullptr;
    decltype(&SysFreeString) freeString=nullptr;
    decltype(&VariantClear) clear=nullptr;
    decltype(&SafeArrayGetLBound) lower=nullptr;
    decltype(&SafeArrayGetUBound) upper=nullptr;
    decltype(&SafeArrayGetElement) element=nullptr;
    bool initialized=false,ready=false;
    Runtime() {
        // COM/WMI is loaded only by the short-lived helper, never the tray.
        ole=LoadLibraryExW(L"ole32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        automation=LoadLibraryExW(L"oleaut32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!ole || !automation)return;
#define LOAD(member,module,symbol) member=reinterpret_cast<decltype(member)>(GetProcAddress(module,symbol));if(!member)return
        LOAD(init,ole,"CoInitializeEx");
        LOAD(security,ole,"CoInitializeSecurity");
        LOAD(uninit,ole,"CoUninitialize");
        LOAD(create,ole,"CoCreateInstance");
        LOAD(blanket,ole,"CoSetProxyBlanket");
        LOAD(alloc,automation,"SysAllocString");
        LOAD(freeString,automation,"SysFreeString");
        LOAD(clear,automation,"VariantClear");
        LOAD(lower,automation,"SafeArrayGetLBound");
        LOAD(upper,automation,"SafeArrayGetUBound");
        LOAD(element,automation,"SafeArrayGetElement");
#undef LOAD
        HRESULT hr=init(nullptr,COINIT_MULTITHREADED);
        initialized=SUCCEEDED(hr);
        if(FAILED(hr) && hr!=RPC_E_CHANGED_MODE)return;
        hr=security(nullptr,-1,nullptr,nullptr,RPC_C_AUTHN_LEVEL_DEFAULT,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE,nullptr);
        ready=SUCCEEDED(hr) || hr==RPC_E_TOO_LATE;
    }
    ~Runtime(){if(initialized)uninit();if(automation)FreeLibrary(automation);if(ole)FreeLibrary(ole);}
};
struct Bstr {
    Runtime& rt;BSTR p;
    Bstr(Runtime& r,const wchar_t* s):rt(r),p(r.alloc(s)){}
    ~Bstr(){if(p)rt.freeString(p);}
};
struct Variant {
    Runtime& rt;VARIANT value{};
    explicit Variant(Runtime& r):rt(r){}
    ~Variant(){if(rt.clear)rt.clear(&value);}
};
class WmiRegistry {
    Runtime rt;
    ComPtr<IWbemLocator> locator;
    ComPtr<IWbemServices> services;
    ComPtr<IWbemClassObject> provider;
    bool ready=false;
    bool putString(IWbemClassObject* object,const wchar_t* name,const wchar_t* text) {
        Variant v(rt);v.value.vt=VT_BSTR;v.value.bstrVal=rt.alloc(text);
        return v.value.bstrVal && SUCCEEDED(object->Put(name,0,&v.value,0));
    }
public:
    WmiRegistry() {
        if(!rt.ready)return;
        if(FAILED(rt.create(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_IWbemLocator,reinterpret_cast<void**>(locator.out()))))return;
        Bstr space(rt,L"ROOT\\DEFAULT"),object(rt,L"StdRegProv");
        if(!space.p || !object.p)return;
        if(FAILED(locator->ConnectServer(space.p,nullptr,nullptr,nullptr,WBEM_FLAG_CONNECT_USE_MAX_WAIT,nullptr,nullptr,services.out())))return;
        if(FAILED(rt.blanket(services.p,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE)))return;
        ready=SUCCEEDED(services->GetObject(object.p,0,nullptr,provider.out(),nullptr));
    }
    bool call(const wchar_t* method,const std::wstring& key,const wchar_t* name,const wchar_t* text,ComPtr<IWbemClassObject>& output,DWORD& code) {
        if(!ready)return false;
        ComPtr<IWbemClassObject> definition,input;
        if(FAILED(provider->GetMethod(method,0,definition.out(),nullptr)) || !definition.p)return false;
        if(FAILED(definition->SpawnInstance(0,input.out())))return false;
        Variant root(rt);root.value.vt=VT_I4;root.value.lVal=static_cast<LONG>(0x80000001u);
        if(FAILED(input->Put(L"hDefKey",0,&root.value,0)) || !putString(input.p,L"sSubKeyName",key.c_str()))return false;
        if(name && !putString(input.p,L"sValueName",name))return false;
        if(text && !putString(input.p,L"sValue",text))return false;
        Bstr object(rt,L"StdRegProv"),action(rt,method);
        if(!object.p || !action.p || FAILED(services->ExecMethod(object.p,action.p,0,nullptr,input.p,output.out(),nullptr)) || !output.p)return false;
        Variant result(rt);
        if(FAILED(output->Get(L"ReturnValue",0,&result.value,nullptr,nullptr)) || (result.value.vt!=VT_I4 && result.value.vt!=VT_UI4))return false;
        code=result.value.ulVal;return true;
    }
    bool contains(const std::wstring& key,const std::wstring& name,bool& exists) {
        ComPtr<IWbemClassObject> output;DWORD code=0;exists=false;
        if(!call(L"EnumValues",key,nullptr,nullptr,output,code))return false;
        if(code==ERROR_FILE_NOT_FOUND)return true;
        if(code!=ERROR_SUCCESS)return false;
        Variant names(rt);
        if(FAILED(output->Get(L"sNames",0,&names.value,nullptr,nullptr)))return false;
        if(names.value.vt==VT_NULL || names.value.vt==VT_EMPTY)return true;
        if(names.value.vt!=(VT_ARRAY|VT_BSTR) || !names.value.parray)return false;
        LONG first=0,last=-1;
        if(FAILED(rt.lower(names.value.parray,1,&first)) || FAILED(rt.upper(names.value.parray,1,&last)) || last-first>4096)return false;
        for(LONG i=first;i<=last;++i) {
            BSTR item=nullptr;
            if(FAILED(rt.element(names.value.parray,&i,&item)))return false;
            bool match=item && CompareStringOrdinal(item,-1,name.c_str(),-1,TRUE)==CSTR_EQUAL;
            if(item)rt.freeString(item);
            if(match){exists=true;break;}
        }
        return true;
    }
    bool readString(const std::wstring& key,const std::wstring& name,std::wstring& text,bool& exists) {
        if(!contains(key,name,exists))return false;
        if(!exists){text.clear();return true;}
        ComPtr<IWbemClassObject> output;DWORD code=0;
        if(!call(L"GetStringValue",key,name.c_str(),nullptr,output,code) || code!=ERROR_SUCCESS)return false;
        Variant value(rt);
        if(FAILED(output->Get(L"sValue",0,&value.value,nullptr,nullptr)) || value.value.vt!=VT_BSTR || !value.value.bstrVal)return false;
        text=value.value.bstrVal;return true;
    }
    bool blocked(const std::wstring& key,const std::wstring& name,bool& disabled) {
        bool exists=false;disabled=false;
        if(!contains(key,name,exists))return false;
        if(!exists)return true;
        ComPtr<IWbemClassObject> output;DWORD code=0;
        if(!call(L"GetBinaryValue",key,name.c_str(),nullptr,output,code) || code!=ERROR_SUCCESS)return false;
        Variant value(rt);
        if(FAILED(output->Get(L"uValue",0,&value.value,nullptr,nullptr)) || value.value.vt!=(VT_ARRAY|VT_UI1) || !value.value.parray)return false;
        LONG first=0,last=-1;BYTE state=0;
        if(FAILED(rt.lower(value.value.parray,1,&first)) || FAILED(rt.upper(value.value.parray,1,&last)) || last-first<11 || FAILED(rt.element(value.value.parray,&first,&state)))return false;
        if(state!=2 && state!=3 && state!=6 && state!=7)return false;
        disabled=state==3 || state==7;return true;
    }
};
class WmiStore:public Store {
    WmiRegistry registry;
    std::wstring key,name,approval;
public:
    WmiStore(std::wstring k=RunPath,std::wstring n=ValueName,std::wstring a=ApprovalPath):key(k),name(n),approval(a){}
    bool read(std::wstring& command,bool& exists)override{return registry.readString(key,name,command,exists);}
    bool write(const std::wstring& command)override {
        ComPtr<IWbemClassObject> created,output;DWORD code=0;
        if(!registry.call(L"CreateKey",key,nullptr,nullptr,created,code) || code!=ERROR_SUCCESS)return false;
        return registry.call(L"SetStringValue",key,name.c_str(),command.c_str(),output,code) && code==ERROR_SUCCESS;
    }
    bool remove()override {
        bool exists=false;if(!registry.contains(key,name,exists))return false;if(!exists)return true;
        ComPtr<IWbemClassObject> output;DWORD code=0;
        return registry.call(L"DeleteValue",key,name.c_str(),nullptr,output,code) && (code==ERROR_SUCCESS || code==ERROR_FILE_NOT_FOUND);
    }
    bool blocked(bool& disabled)override{return registry.blocked(approval,name,disabled);}
};
std::wstring executablePath() {
    std::vector<wchar_t> buffer(32768);DWORD n=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    return n && n<buffer.size()?std::wstring(buffer.data(),n):L"";
}
State invoke(const wchar_t* argument,HANDLE cancellation) {
    if(cancellation && WaitForSingleObject(cancellation,0)==WAIT_OBJECT_0)return State::Unavailable;
    std::wstring path=executablePath();
    if(path.empty())return State::Unavailable;
    std::wstring line=L"\""+path+L"\" "+argument;
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi{};
    HANDLE job=CreateJobObjectW(nullptr,nullptr);
    if(!job)return State::Unavailable;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){CloseHandle(job);return State::Unavailable;}
    if(!CreateProcessW(path.c_str(),&line[0],nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&si,&pi)){CloseHandle(job);return State::Unavailable;}
    bool assigned=AssignProcessToJobObject(job,pi.hProcess)!=FALSE;
    bool resumed=assigned && ResumeThread(pi.hThread)!=static_cast<DWORD>(-1);
    if(!resumed)TerminateProcess(pi.hProcess,static_cast<UINT>(State::Unavailable));
    DWORD wait=WAIT_FAILED;
    if(resumed) {
        if(cancellation) {
            HANDLE events[]={pi.hProcess,cancellation};
            wait=WaitForMultipleObjects(2,events,FALSE,8000);
        } else wait=WaitForSingleObject(pi.hProcess,8000);
    }
    DWORD result=static_cast<DWORD>(State::Unavailable);
    if(wait==WAIT_OBJECT_0)GetExitCodeProcess(pi.hProcess,&result);
    // Closing this job also cancels a hung helper; no orphan process remains.
    CloseHandle(job);
    if(wait!=WAIT_OBJECT_0)WaitForSingleObject(pi.hProcess,1000);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    switch(static_cast<State>(result)) {
        case State::Off:case State::On:case State::OtherPath:case State::WindowsDisabled:return static_cast<State>(result);
        default:return State::Unavailable;
    }
}
}
State inspect(HANDLE cancellation){return invoke(L"--startup-query",cancellation);}
State turnOn(HANDLE cancellation){return invoke(L"--startup-enable",cancellation);}
State turnOff(HANDLE cancellation){return invoke(L"--startup-disable",cancellation);}
State runHelper(const char* operation) {
    std::wstring path=executablePath();
    if(path.empty())return State::Unavailable;
    std::wstring command=L"\""+path+L"\"";
    WmiStore store;
    if(strcmp(operation,"--startup-query")==0)return query(store,command);
    if(strcmp(operation,"--startup-enable")==0)return enable(store,command);
    if(strcmp(operation,"--startup-disable")==0)return disable(store,command);
    return State::Unavailable;
}
}
