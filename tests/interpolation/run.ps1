$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Push-Location -LiteralPath $repoRoot
try {
    New-Item -ItemType Directory -Force artifacts\interpolation | Out-Null
    $source = [IO.File]::ReadAllText((Join-Path $repoRoot 'src\AmtPtpDeviceUsbUm\InputInterrupt.c')).Replace("`r`n", "`n")
    $start = $source.IndexOf("static double`nAmtPtpInterpAlpha(")
    $end = $source.IndexOf("VOID`nAmtPtpInterpolationTimerCallback(", $start)
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
} finally {
    Pop-Location
}
