// Exercise the real overlapped Channel against Windows named pipes, without
// requiring hardware disconnection or changing any mouse settings.
#include "../src/hid_device.cpp"
struct Scenario {HANDLE server,stop;int mode;};
bool readPacket(HANDLE pipe,BYTE* p) {DWORD n=0;return ReadFile(pipe,p,20,&n,nullptr) && n==20;}
bool sendPacket(HANDLE pipe,const BYTE* p) {DWORD n=0;return WriteFile(pipe,p,20,&n,nullptr) && n==20;}
DWORD WINAPI serve(void* arg) {
    auto& s=*static_cast<Scenario*>(arg);BYTE p[20]{};
    if(!readPacket(s.server,p)) return 1;
    if(s.mode==0) {
        BYTE unrelated[20];memcpy(unrelated,p,20);unrelated[1]=2;sendPacket(s.server,unrelated);
        memcpy(unrelated,p,20);unrelated[2]^=1;sendPacket(s.server,unrelated);
        memcpy(unrelated,p,20);unrelated[3]^=1;sendPacket(s.server,unrelated);
        p[4]=85;sendPacket(s.server,p);
    } else if(s.mode==1) {
        BYTE unrelated[20];memcpy(unrelated,p,20);unrelated[3]&=0xf0;
        for(int i=0;i<30;++i) {if(!sendPacket(s.server,unrelated)) break;Sleep(30);}
    } else if(s.mode==2) {
        Sleep(850);p[4]=12;sendPacket(s.server,p);
        if(readPacket(s.server,p)) {p[4]=86;sendPacket(s.server,p);}
    } else if(s.mode==3) {Sleep(40);SetEvent(s.stop);}
    else if(s.mode==4) {
        // The first request gets no radio reply; a second ping must recover.
        if(readPacket(s.server,p)) {BYTE nonce=p[6];p[4]=4;p[5]=2;p[6]=nonce;sendPacket(s.server,p);}
    } else if(s.mode==5 || s.mode==6) {
        BYTE nonce=p[6];p[4]=4;p[5]=2;p[6]=nonce^0xff;sendPacket(s.server,p);
        if(s.mode==6) {Sleep(40);SetEvent(s.stop);}
        else if(readPacket(s.server,p)) {nonce=p[6];p[4]=4;p[5]=2;p[6]=nonce;sendPacket(s.server,p);}
    }
    return 0;
}
int main() {
    int checks=0;
    for(int mode=0;mode<7;++mode) {
        wchar_t name[128];swprintf(name,128,L"\\\\.\\pipe\\LogiBatteryTransportTest-%lu-%d",GetCurrentProcessId(),mode);
        OwnedHandle server(CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT,1,4096,4096,0,nullptr));
        OwnedHandle client(CreateFileW(name,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
        OwnedHandle stop(CreateEventW(nullptr,TRUE,FALSE,nullptr));
        if(server.h==INVALID_HANDLE_VALUE || client.h==INVALID_HANDLE_VALUE) {printf("FAIL pipe setup %lu\n",GetLastError());return 1;}
        ConnectNamedPipe(server.h,nullptr);
        Scenario scenario{server.h,stop.h,mode};OwnedHandle thread(CreateThread(nullptr,0,serve,&scenario,0,nullptr));
        Endpoint endpoint{L"test",20,20};Channel channel(client.h,stop.h,endpoint,false);
        std::vector<BYTE> r;ULONGLONG begin=GetTickCount64();
        bool ok=mode>=4?channel.ping(1):channel.request(1,6,1,nullptr,0,r);ULONGLONG elapsed=GetTickCount64()-begin;
        bool valid=false;
        if(mode==0) valid=ok && r.size()==16 && r[0]==85;
        if(mode==1) valid=!ok && elapsed>=650 && elapsed<1200;
        if(mode==2) {
            valid=!ok && elapsed>=650 && elapsed<1200;
            bool second=channel.request(1,6,1,nullptr,0,r);
            valid=valid && second && r[0]==86;
        }
        if(mode==3) valid=!ok && elapsed<500;
        if(mode==4) valid=ok && elapsed>=850 && elapsed<1700;
        if(mode==5) valid=ok && elapsed>=120 && elapsed<800;
        if(mode==6) valid=!ok && elapsed<500;
        WaitForSingleObject(thread.h,2000);
        if(!valid) {printf("FAIL transport mode=%d ok=%d elapsed=%llu\n",mode,ok,elapsed);return 1;}
        printf("PASS transport mode=%d elapsed=%llums\n",mode,elapsed);++checks;
    }
    printf("PASS %d Windows transport scenarios\n",checks);
}
