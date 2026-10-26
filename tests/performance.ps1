param([int]$Seconds=70,[string]$ProcessName='BatteryTray')
$ErrorActionPreference='Stop'
$samples=@()
$elapsed=0
do {
    $p=Get-Process -Name $ProcessName
    if (@($p).Count -ne 1) { throw 'Expected exactly one tray process' }
    $samples += [pscustomobject]@{elapsed=$elapsed;workingSet=$p.WorkingSet64;privateBytes=$p.PrivateMemorySize64;cpuSeconds=$p.CPU;handles=$p.HandleCount;threads=$p.Threads.Count}
    if ($p.PrivateMemorySize64 -gt 15MB) { throw "Private memory exceeds trial budget: $($p.PrivateMemorySize64)" }
    if ($elapsed -ge $Seconds) { break }
    Start-Sleep -Seconds 10
    $elapsed+=10
} while($true)
$samples | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot '../build/resource-samples-final.json')
$samples | Format-Table
$delta=$samples[-1].cpuSeconds-$samples[0].cpuSeconds
[pscustomobject]@{cpuSeconds=$delta;oneCoreCpuPercent=if($elapsed){$delta/$elapsed*100}else{0};maxPrivateBytes=($samples.privateBytes | Measure-Object -Maximum).Maximum} | ConvertTo-Json
if ($elapsed -gt 0 -and $delta/$elapsed -gt 0.01) { throw 'CPU exceeds 1% of one core during idle sampling' }
