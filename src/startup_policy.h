#pragma once
#include <windows.h>
#include <string>

namespace startup {
enum class State : unsigned long { Off=0, On=10, OtherPath=11, WindowsDisabled=12, Unavailable=20 };
class Store {
public:
    virtual ~Store()=default;
    virtual bool read(std::wstring& command,bool& exists)=0;
    virtual bool write(const std::wstring& command)=0;
    virtual bool remove()=0;
    virtual bool blocked(bool& disabled)=0;
};
inline State query(Store& store,const std::wstring& command) {
    if(command.empty() || command.size()>260)return State::Unavailable;
    std::wstring saved;bool exists=false,disabled=false;
    if(!store.read(saved,exists)) return State::Unavailable;
    if(!exists) return State::Off;
    if(CompareStringOrdinal(saved.c_str(),-1,command.c_str(),-1,TRUE)!=CSTR_EQUAL) return State::OtherPath;
    if(!store.blocked(disabled)) return State::Unavailable;
    return disabled?State::WindowsDisabled:State::On;
}
inline State enable(Store& store,const std::wstring& command) {
    // Windows Run command lines have a documented 260-character limit.
    if(command.empty() || command.size()>260 || !store.write(command)) return State::Unavailable;
    State result=query(store,command);
    return result==State::On || result==State::WindowsDisabled?result:State::Unavailable;
}
inline State disable(Store& store,const std::wstring&) {
    if(!store.remove()) return State::Unavailable;
    std::wstring saved;bool exists=true;
    return store.read(saved,exists) && !exists?State::Off:State::Unavailable;
}
}
