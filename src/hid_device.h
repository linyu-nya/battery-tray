#pragma once
#include <windows.h>
#include <string>
#include "battery.h"
struct Reading {
    bool online=false;
    Battery battery;
    std::wstring name,identity;
    std::wstring error=L"设备暂未响应，正在自动重试；可单击立即刷新";
    SYSTEMTIME time{};
    unsigned short feature=0;
};
Reading queryBattery(HANDLE stop, bool verbose=false);
