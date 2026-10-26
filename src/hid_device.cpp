#include "hid_device.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <vector>
#include <algorithm>
#include <stdio.h>

namespace {
struct OwnedHandle {
    HANDLE h;
    explicit OwnedHandle(HANDLE v):h(v){}
    ~OwnedHandle(){ if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h); }
    OwnedHandle(const OwnedHandle&)=delete;
    OwnedHandle& operator=(const OwnedHandle&)=delete;
};
bool stopped(HANDLE stop){ return WaitForSingleObject(stop,0)==WAIT_OBJECT_0; }
struct Endpoint { std::wstring path; USHORT input,output; };
std::vector<Endpoint> enumerate(bool verbose) {
    std::vector<Endpoint> found;
    GUID guid; HidD_GetHidGuid(&guid);
    HDEVINFO set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE) return found;
    SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize=sizeof(iface);
    for(DWORD i=0;SetupDiEnumDeviceInterfaces(set,nullptr,&guid,i,&iface);++i) {
        DWORD size=0;
        SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&size,nullptr);
        if(size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<BYTE> mem(size);
        auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(mem.data());
        detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,size,nullptr,nullptr)) continue;
        OwnedHandle file(CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
        if(file.h==INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES attrs{}; attrs.Size=sizeof(attrs);
        if(!HidD_GetAttributes(file.h,&attrs) || attrs.VendorID!=0x046d) continue;
        PHIDP_PREPARSED_DATA data=nullptr; HIDP_CAPS caps{};
        if(!HidD_GetPreparsedData(file.h,&data)) continue;
        NTSTATUS status=HidP_GetCaps(data,&caps); HidD_FreePreparsedData(data);
        if(status!=HIDP_STATUS_SUCCESS) continue;
        if(verbose) fwprintf(stderr,L"HID pid=%04x usage=%04x:%04x input=%u output=%u\n",attrs.ProductID,caps.UsagePage,caps.Usage,caps.InputReportByteLength,caps.OutputReportByteLength);
        if(caps.UsagePage>=0xff00 && caps.InputReportByteLength>=7 && caps.InputReportByteLength<=64 &&
           (caps.OutputReportByteLength==7 || caps.OutputReportByteLength==20))
            found.push_back({detail->DevicePath,caps.InputReportByteLength,caps.OutputReportByteLength});
    }
    SetupDiDestroyDeviceInfoList(set);
    std::stable_sort(found.begin(),found.end(),[](const Endpoint&a,const Endpoint&b){return a.output>b.output;});
    return found;
}
class Channel {
    HANDLE file,stop;
    USHORT input,output;
    BYTE sid;
    bool verbose;
    bool protocolError=false;
    bool io(bool write,BYTE* data,DWORD size,DWORD timeout,DWORD& done) {
        OVERLAPPED ov{}; OwnedHandle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
        if(!event.h || stopped(stop)) return false;
        ov.hEvent=event.h; done=0;
        BOOL ok=write?WriteFile(file,data,size,&done,&ov):ReadFile(file,data,size,&done,&ov);
        if(ok) return true;
        if(GetLastError()!=ERROR_IO_PENDING) return false;
        HANDLE events[]={stop,event.h};
        DWORD result=WaitForMultipleObjects(2,events,FALSE,timeout);
        if(result==WAIT_OBJECT_0+1) return GetOverlappedResult(file,&ov,&done,FALSE)!=FALSE;
        // Completion is drained before ov, event and buffer leave scope.
        CancelIoEx(file,&ov); GetOverlappedResult(file,&ov,&done,TRUE);
        return false;
    }
public:
    Channel(HANDLE f,HANDLE s,const Endpoint&e,bool v):file(f),stop(s),input(e.input),output(e.output),sid((GetTickCount()%15)+1),verbose(v){}
    bool ping(BYTE device) {
        for(int attempt=0;attempt<2 && !stopped(stop);++attempt) {
            BYTE nonce[]={0,0,static_cast<BYTE>(GetTickCount())}; std::vector<BYTE> r;
            if(request(device,0,1,nonce,3,r) && r.size()>=3 && r[0]>=2 && r[2]==nonce[2]) return true;
            if(protocolError || attempt==1 || WaitForSingleObject(stop,150)!=WAIT_TIMEOUT) break;
        }
        return false;
    }
    bool request(BYTE device,BYTE feature,BYTE function,const BYTE* params,size_t length,std::vector<BYTE>& reply) {
        protocolError=false;
        if(length>3 || stopped(stop)) return false;
        sid=static_cast<BYTE>(sid%15+1);
        BYTE command=static_cast<BYTE>((function<<4)|sid);
        std::vector<BYTE> packet(output,0); packet[0]=output==7?0x10:0x11;
        packet[1]=device; packet[2]=feature; packet[3]=command;
        for(size_t i=0;i<length;++i) packet[4+i]=params[i];
        const ULONGLONG deadline=GetTickCount64()+750;
        DWORD count=0;
        if(!io(true,packet.data(),output,750,count) || count!=output) return false;
        while(!stopped(stop)) {
            ULONGLONG now=GetTickCount64(); if(now>=deadline) return false;
            std::vector<BYTE> response(input,0);
            if(!io(false,response.data(),input,static_cast<DWORD>(deadline-now),count)) return false;
            if(count<7 || (response[0]==0x11 && count<20)) continue;
            Reply kind=match(response.data(),count,device,feature,command);
            if(kind==Reply::Ignore) continue;
            if(verbose) { fprintf(stderr,"reply dev=%02x feature=%02x command=%02x:",device,feature,command); for(DWORD i=0;i<count;++i) fprintf(stderr," %02x",response[i]); fprintf(stderr,"\n"); }
            if(kind==Reply::Error) {protocolError=true;return false;}
            size_t useful=response[0]==0x10?7:20;
            reply.assign(response.begin()+4,response.begin()+useful);
            return true;
        }
        return false;
    }
    BYTE feature(BYTE device,USHORT id) {
        BYTE p[]={static_cast<BYTE>(id>>8),static_cast<BYTE>(id),0};
        std::vector<BYTE> r;
        return request(device,0,0,p,3,r) && r.size()>=3 ? r[0]:0;
    }
};
}

Reading queryBattery(HANDLE stop,bool verbose) {
    Reading result;
    auto endpoints=enumerate(verbose);
    if(endpoints.empty()) { result.error=L"未发现可用的罗技无线接收器"; return result; }
    for(const auto& e:endpoints) {
        if(stopped(stop)) break;
        OwnedHandle file(CreateFileW(e.path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));
        if(file.h==INVALID_HANDLE_VALUE) { result.error=L"无法访问接收器：可能被其它程序独占"; continue; }
        Channel channel(file.h,stop,e,verbose);
        const BYTE devices[]={1,2,0xff};
        for(BYTE device:devices) {
            if(stopped(stop)) break;
            std::vector<BYTE> r;
            if(!channel.ping(device)) continue;
            USHORT batteryFeature=0x1004;
            BYTE index=channel.feature(device,batteryFeature);
            if(!index) { batteryFeature=0x1000; index=channel.feature(device,batteryFeature); }
            if(!index) continue;
            bool exact=true;
            if(batteryFeature==0x1004) {
                if(!channel.request(device,index,0,nullptr,0,r) || r.size()<2) continue;
                exact=(r[1]&2)!=0;
            }
            if(!channel.request(device,index,batteryFeature==0x1004?1:0,nullptr,0,r)) continue;
            Battery battery;
            if(!decode(batteryFeature,r.data(),r.size(),exact,battery)) continue;
            std::wstring name=L"Logitech Mouse";
            BYTE nameIndex=channel.feature(device,5);
            if(nameIndex && channel.request(device,nameIndex,0,nullptr,0,r) && !r.empty()) {
                size_t length=std::min<size_t>(r[0],120); std::string utf8;
                while(utf8.size()<length) {
                    BYTE offset=static_cast<BYTE>(utf8.size());
                    if(!channel.request(device,nameIndex,1,&offset,1,r) || r.empty()) break;
                    size_t take=std::min(length-utf8.size(),r.size());
                    utf8.append(reinterpret_cast<const char*>(r.data()),take);
                }
                if(utf8.size()==length && length) {
                    int n=MultiByteToWideChar(CP_UTF8,0,utf8.data(),static_cast<int>(length),nullptr,0);
                    if(n>0) { name.resize(n); MultiByteToWideChar(CP_UTF8,0,utf8.data(),static_cast<int>(length),&name[0],n); }
                }
            }
            result.online=true; result.battery=battery; result.name=name; result.feature=batteryFeature;
            result.identity=e.path+std::to_wstring(device)+name;
            result.error.clear(); GetLocalTime(&result.time);
            return result;
        }
    }
    return result;
}
