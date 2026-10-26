param([switch]$TestsOnly,[string]$Output='bin/BatteryTray.exe')
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
$zig = Join-Path $PSScriptRoot '.tools\fast\zig-windows-x86_64-0.13.0\zig.exe'
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot '.tools\full\zig-windows-x86_64-0.13.0\zig.exe')) { $zig=Join-Path $PSScriptRoot '.tools\full\zig-windows-x86_64-0.13.0\zig.exe' }
if (!(Test-Path -LiteralPath $zig)) {
    New-Item -ItemType Directory -Force -Path '.tools\fast' | Out-Null
    $archive = Join-Path $PSScriptRoot '.tools\zig.zip'
    & curl.exe -L --fail --retry 3 -o $archive 'https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip'
    if ($LASTEXITCODE -ne 0) { throw 'Compiler download failed' }
    $expected = 'd859994725ef9402381e557c60bb57497215682e355204d754ee3df75ee3c158'
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Compiler SHA256 mismatch' }
    & tar.exe -xf $archive -C '.tools\fast'
    if ($LASTEXITCODE -ne 0) { throw 'Compiler extraction failed' }
}
New-Item -ItemType Directory -Force -Path bin,build | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot '.tools\cache'
$flags = @('-target','x86_64-windows-gnu','-std=c++17','-O2','-Wall','-Wextra','-fno-exceptions','-fno-rtti','-DUNICODE','-D_UNICODE')
& $zig c++ @flags tests/battery_test.cpp -o build/battery_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
& .\build\battery_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Protocol tests failed' }
& $zig c++ @flags tests/recovery_test.cpp -o build/recovery_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Recovery test compilation failed' }
& .\build\recovery_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Recovery tests failed' }
& $zig c++ @flags tests/icon_test.cpp -o build/icon_test.exe -lgdi32 -luser32
if ($LASTEXITCODE -ne 0) { throw 'Icon test compilation failed' }
& .\build\icon_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Icon tests failed' }
& $zig c++ @flags tests/contrast_test.cpp -o build/contrast_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Contrast test compilation failed' }
& .\build\contrast_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Contrast tests failed' }
& $zig c++ @flags tests/transport_test.cpp -o build/transport_test.exe -lsetupapi -lhid
if ($LASTEXITCODE -ne 0) { throw 'Transport test compilation failed' }
& .\build\transport_test.exe
if ($LASTEXITCODE -ne 0) { throw 'Transport tests failed' }
if (!$TestsOnly) {
    & $zig c++ @flags -s '-Wl,--subsystem,windows' src/main.cpp src/hid_device.cpp -o $Output -lsetupapi -lhid -lshell32 -lgdi32 -ladvapi32 -luser32 -limm32
    if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed' }
    Get-Item -LiteralPath $Output | Select-Object FullName,Length
}
