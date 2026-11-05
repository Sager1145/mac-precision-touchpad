$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Push-Location -LiteralPath $root
try {
    New-Item -ItemType Directory -Force artifacts\bluetooth | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -Wno-attributes -I tests/interpolation/stubs `
        -isystem src/AmtPtpDeviceUsbUm/include tests/bluetooth/test.c -o artifacts/bluetooth/test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Bluetooth packet tests failed to compile' }
    & .\artifacts\bluetooth\test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Bluetooth packet regression failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -Wno-attributes -I tests/interpolation/stubs `
        -isystem src/AmtPtpDeviceUsbUm/include tests/bluetooth/resampling.c -o artifacts/bluetooth/resampling.exe
    if ($LASTEXITCODE -ne 0) { throw 'Bluetooth resampling tests failed to compile' }
    & .\artifacts\bluetooth\resampling.exe
    if ($LASTEXITCODE -ne 0) { throw 'Bluetooth resampling regression failed' }
} finally { Pop-Location }
