$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Push-Location -LiteralPath $root
try {
    New-Item -ItemType Directory -Force artifacts\transport-parity | Out-Null
    $source = [IO.File]::ReadAllText((Join-Path $root 'src\AmtPtpDeviceUsbUm\InputInterrupt.c')).Replace("`r`n", "`n")
    $start = $source.IndexOf("static double`nAmtPtpInterpAlpha(")
    $end = $source.IndexOf("VOID`nAmtPtpInterpolationTick(", $start)
    $decoder = $source.IndexOf("NTSTATUS`nAmtPtpServiceTouchInputInterruptType5(")
    if ($start -lt 0 -or $end -lt 0 -or $decoder -lt 0) { throw 'Production helper boundaries missing' }
    $extracted = $source.Substring($start, $end - $start) + $source.Substring($decoder, $start - $decoder)
    [IO.File]::WriteAllText((Join-Path $root 'artifacts\transport-parity\usb-under-test.inc'), $extracted)
    & gcc -std=c11 -Wall -Wextra -Werror -Wno-attributes `
        -I tests/interpolation/stubs -I artifacts/transport-parity `
        -isystem src/AmtPtpDeviceUsbUm/include tests/transport-parity/test.c `
        -o artifacts/transport-parity/test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Transport parity test compilation failed' }
    & .\artifacts\transport-parity\test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Transport parity regression failed' }
} finally { Pop-Location }
