$ErrorActionPreference='Stop'
$testKey='Software\BatteryTray\StartupTests\'+[Guid]::NewGuid().ToString('N')
$approvalKey=$testKey+'\Approved'
$root=[uint32]2147483649
$fixtures=@{Enabled2=[byte]2;Disabled3=[byte]3;Enabled6=[byte]6;Disabled7=[byte]7}
try {
    foreach($key in @($testKey,$approvalKey)) {
        $result=Invoke-CimMethod -Namespace root/default -ClassName StdRegProv -MethodName CreateKey -Arguments @{hDefKey=$root;sSubKeyName=$key}
        if($result.ReturnValue -ne 0){throw 'Startup test key creation failed'}
    }
    foreach($name in $fixtures.Keys) {
        $bytes=New-Object byte[] 12
        $bytes[0]=$fixtures[$name]
        $result=Invoke-CimMethod -Namespace root/default -ClassName StdRegProv -MethodName SetBinaryValue -Arguments @{hDefKey=$root;sSubKeyName=$approvalKey;sValueName=$name;uValue=$bytes}
        if($result.ReturnValue -ne 0){throw 'Startup approval fixture creation failed'}
    }
    $result=Invoke-CimMethod -Namespace root/default -ClassName StdRegProv -MethodName SetBinaryValue -Arguments @{hDefKey=$root;sSubKeyName=$approvalKey;sValueName='Malformed';uValue=[byte[]]@(2)}
    if($result.ReturnValue -ne 0){throw 'Malformed approval fixture creation failed'}
    & (Join-Path $PSScriptRoot '../build/startup_system_test.exe') $testKey
    if($LASTEXITCODE -ne 0){throw 'Native startup system checks failed'}
    foreach($name in $fixtures.Keys) {
        $result=Invoke-CimMethod -Namespace root/default -ClassName StdRegProv -MethodName GetBinaryValue -Arguments @{hDefKey=$root;sSubKeyName=$approvalKey;sValueName=$name}
        if($result.ReturnValue -ne 0 -or $result.uValue.Count -ne 12 -or $result.uValue[0] -ne $fixtures[$name]){throw 'Startup check modified Windows approval state'}
    }
    Write-Output 'PASS Windows disabled approval records preserved'
} finally {
    foreach($key in @($approvalKey,$testKey)) {
        $result=Invoke-CimMethod -Namespace root/default -ClassName StdRegProv -MethodName DeleteKey -Arguments @{hDefKey=$root;sSubKeyName=$key}
        if($result.ReturnValue -ne 0 -and $result.ReturnValue -ne 2){throw 'Startup test key cleanup failed'}
    }
}
