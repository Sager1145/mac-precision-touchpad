$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Push-Location -LiteralPath $repoRoot
try {
    New-Item -ItemType Directory -Force artifacts\legacy-usb | Out-Null
    $source = [IO.File]::ReadAllText((Join-Path $repoRoot 'src\AmtPtpDeviceUsbUm\InputInterrupt.c')).Replace("`r`n", "`n")
    $start = $source.IndexOf("NTSTATUS`nAmtPtpServiceTouchInputInterrupt(")
    $end = $source.IndexOf('_IRQL_requires_(PASSIVE_LEVEL)', $start)
    $clockStart = $source.IndexOf("static VOID`nAmtPtpSetScanTime(", $end)
    $clockEnd = $source.IndexOf("static NTSTATUS`nAmtPtpEmitReport(", $clockStart)
    if ($start -lt 0 -or $end -lt 0 -or $clockStart -lt 0 -or $clockEnd -lt 0) { throw 'Legacy input helper boundaries missing' }
    # Exercise the production decoder and clock, including injected WDF failures.
    $extracted = $source.Substring($clockStart, $clockEnd - $clockStart) + $source.Substring($start, $end - $start)
    [IO.File]::WriteAllText((Join-Path $repoRoot 'artifacts\legacy-usb\input-under-test.inc'), $extracted)
    & gcc -std=c11 -Wall -Wextra -Werror -Wno-attributes `
        -I tests/interpolation/stubs -I artifacts/legacy-usb `
        -isystem src/AmtPtpDeviceUsbUm/include tests/legacy-usb/test.c `
        -o artifacts/legacy-usb/test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Legacy USB test compilation failed' }
    & .\artifacts\legacy-usb\test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Legacy USB regression failed' }
} finally {
    Pop-Location
}
