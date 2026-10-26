param([string]$ProcessName='BatteryTray')
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LogiMemoryMetrics {
 [StructLayout(LayoutKind.Sequential)] public struct Counters {
  public uint cb, faults;
  public UIntPtr peakWorkingSet, workingSet, peakPaged, paged, peakNonPaged, nonPaged, pageFile, peakPageFile, privateUsage, privateWorkingSet;
  public ulong sharedCommit;
 }
 [DllImport("psapi.dll", SetLastError=true)] public static extern bool GetProcessMemoryInfo(IntPtr process, ref Counters counters, uint size);
}
'@
foreach ($p in Get-Process -Name $ProcessName) {
    $c=New-Object LogiMemoryMetrics+Counters
    $c.cb=[Runtime.InteropServices.Marshal]::SizeOf($c)
    if(![LogiMemoryMetrics]::GetProcessMemoryInfo($p.Handle,[ref]$c,$c.cb)) { throw "GetProcessMemoryInfo failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
    [pscustomobject]@{pid=$p.Id;name=$p.Name;workingSetBytes=$c.workingSet.ToUInt64();privateWorkingSetBytes=$c.privateWorkingSet.ToUInt64();privateCommitBytes=$c.privateUsage.ToUInt64();workingSetMiB=[math]::Round($c.workingSet.ToUInt64()/1MB,3);privateWorkingSetMiB=[math]::Round($c.privateWorkingSet.ToUInt64()/1MB,3);privateCommitMiB=[math]::Round($c.privateUsage.ToUInt64()/1MB,3)} | ConvertTo-Json
}
