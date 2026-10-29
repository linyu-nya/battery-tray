#include "../src/startup_policy.h"
#include <stdio.h>
using startup::State;
struct FakeStore:startup::Store {
    std::wstring systemCommand;
    bool exists=false,writeVisible=true,removeVisible=true,accessible=true,isBlocked=false,writeAllowed=true;
    bool read(std::wstring& c,bool& e) override {c=systemCommand;e=exists;return accessible;}
    bool write(const std::wstring& c) override {if(!writeAllowed)return false;if(writeVisible){systemCommand=c;exists=true;}return true;}
    bool remove() override {if(removeVisible)exists=false;return true;}
    bool blocked(bool& b) override {b=isBlocked;return accessible;}
};
#define CHECK(x) do{if(!(x)){printf("FAIL startup policy line %d: %s\n",__LINE__,#x);return 1;}++count;}while(0)
int main() {
    int count=0;FakeStore s;const std::wstring command=L"\"D:\\Apps\\BatteryTray.exe\"";
    s.writeVisible=false;
    CHECK(startup::enable(s,command)==State::Unavailable);
    CHECK(startup::query(s,command)==State::Off);
    s.writeVisible=true;
    CHECK(startup::enable(s,command)==State::On);
    CHECK(startup::query(s,command)==State::On);
    s.isBlocked=true;
    CHECK(startup::query(s,command)==State::WindowsDisabled);
    CHECK(startup::enable(s,command)==State::WindowsDisabled);
    s.isBlocked=false;s.systemCommand=L"\"D:\\Old\\BatteryTray.exe\"";
    CHECK(startup::query(s,command)==State::OtherPath);
    CHECK(startup::enable(s,command)==State::On);
    s.removeVisible=false;
    CHECK(startup::disable(s,command)==State::Unavailable);
    s.removeVisible=true;
    CHECK(startup::disable(s,command)==State::Off);
    s.accessible=false;
    CHECK(startup::query(s,command)==State::Unavailable);
    CHECK(startup::enable(s,command)==State::Unavailable);
    s.accessible=true;s.writeAllowed=false;
    CHECK(startup::enable(s,command)==State::Unavailable);
    s.writeAllowed=true;
    CHECK(startup::enable(s,std::wstring(261,L'x'))==State::Unavailable);
    s.systemCommand=std::wstring(261,L'x');s.exists=true;
    CHECK(startup::query(s,s.systemCommand)==State::Unavailable);
    CHECK(startup::query(s,L"")==State::Unavailable);
    CHECK(startup::disable(s,s.systemCommand)==State::Off);
    printf("PASS %d startup policy checks\n",count);
}
