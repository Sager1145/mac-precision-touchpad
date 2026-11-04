$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Push-Location -LiteralPath $repoRoot
try {
    New-Item -ItemType Directory -Force artifacts\interpolation | Out-Null
    $source = [IO.File]::ReadAllText((Join-Path $repoRoot 'src\AmtPtpDeviceUsbUm\InputInterrupt.c')).Replace("`r`n", "`n")
    $start = $source.IndexOf("static double`nAmtPtpInterpAlpha(")
    $end = $source.IndexOf("VOID`nAmtPtpInterpolationTick(", $start)
    if ($start -lt 0 -or $end -lt 0) { throw 'Interpolation helper boundaries missing' }
    # Compile the actual production helpers rather than a copy of the algorithm.
    [IO.File]::WriteAllText((Join-Path $repoRoot 'artifacts\interpolation\interpolation-under-test.inc'), $source.Substring($start, $end - $start))
    & gcc -std=c11 -Wall -Wextra -Werror -Wno-attributes `
        -I tests/interpolation/stubs -I artifacts/interpolation `
        -isystem src/AmtPtpDeviceUsbUm/include tests/interpolation/test.c `
        -o artifacts/interpolation/test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Test compilation failed' }
    & .\artifacts\interpolation\test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Interpolation regression failed' }
    $deviceSource = [IO.File]::ReadAllText((Join-Path $repoRoot 'src\AmtPtpDeviceUsbUm\Device.c')).Replace("`r`n", "`n")
    $clockStart = $deviceSource.IndexOf("static DWORD WINAPI`nAmtPtpResamplingThread(")
    $clockEnd = $deviceSource.IndexOf('_IRQL_requires_(PASSIVE_LEVEL)', $clockStart)
    if ($clockStart -lt 0 -or $clockEnd -lt 0) { throw 'Native clock boundaries missing' }
    [IO.File]::WriteAllText((Join-Path $repoRoot 'artifacts\interpolation\native-clock-under-test.inc'), $deviceSource.Substring($clockStart, $clockEnd - $clockStart))
    & gcc -std=c11 -Wall -Wextra -Werror -Wno-attributes `
        -I tests/interpolation/stubs -I artifacts/interpolation `
        -isystem src/AmtPtpDeviceUsbUm/include tests/interpolation/native-clock.c `
        -o artifacts/interpolation/native-clock.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native clock test compilation failed' }
    & .\artifacts\interpolation\native-clock.exe
    if ($LASTEXITCODE -ne 0) { throw 'Native clock regression failed' }
} finally {
    Pop-Location
}
