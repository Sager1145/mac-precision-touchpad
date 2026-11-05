param(
    [string]$Version = '0.2.0',
    [string]$InnoCompiler = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    [string]$SignTool = 'D:\Program Files\Windows Kits\10\bin\10.0.28000.0\x64\signtool.exe',
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ($Version -notmatch '^\d+\.\d+\.\d+(-[a-zA-Z0-9.]+)?$') { throw 'Invalid release version.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root "artifacts\release\v$Version" }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$staging = Join-Path $output 'package'
$common = Join-Path $staging 'common'
New-Item -ItemType Directory -Force $common | Out-Null
$thumbprint = '94A13BEE28D788FC4A7FA91C568968C7DBADDC69'
foreach ($tool in @($InnoCompiler,$SignTool)) { if (-not (Test-Path -LiteralPath $tool)) { throw "Build tool missing: $tool" } }
$revision = & git -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Unable to identify the source commit.' }

function Assert-Machine([string]$Path, [int]$ExpectedMachine) {
    $reader = New-Object IO.BinaryReader([IO.File]::OpenRead($Path))
    try {
        $reader.BaseStream.Position = 0x3c
        $offset = $reader.ReadInt32()
        $reader.BaseStream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne $ExpectedMachine) {
            throw "Unexpected PE architecture: $Path"
        }
    } finally { $reader.Dispose() }
}

$archives = @()
foreach ($architecture in @('x64','arm64')) {
    $built = Join-Path $root "build\AmtPtpDeviceUniversalPkg\$architecture\Release\AmtPtpDeviceUniversalPkg"
    $payload = Join-Path $staging $architecture
    New-Item -ItemType Directory -Force $payload | Out-Null
    $binaries = @('AmtPtpDeviceUsbUm.dll','AmtPtpHidFilter.sys','AmtPtpDeviceUsbKm.sys','AmtPtpDeviceSpiKm.sys')
    $files = @('AmtPtpDevice.inf','amtptpdevice.cat','AmtPtpHidFilter.inf','amtptphidfilter.cat') + $binaries
    $machine = if ($architecture -eq 'arm64') { 0xaa64 } else { 0x8664 }
    foreach ($file in $binaries) { Assert-Machine (Join-Path $built $file) $machine }
    foreach ($file in $files) { Copy-Item -LiteralPath (Join-Path $built $file) -Destination $payload -Force }
    $certificate = Join-Path $payload 'AmtPtpDeviceTest.cer'
    Copy-Item -LiteralPath (Join-Path (Split-Path $built) 'AmtPtpDeviceUniversalPkg.cer') -Destination $certificate -Force
    $cert = New-Object Security.Cryptography.X509Certificates.X509Certificate2($certificate)
    if ($cert.Thumbprint -ne $thumbprint) { throw "Unexpected $architecture signing certificate." }
    foreach ($file in (@('amtptpdevice.cat','amtptphidfilter.cat') + $binaries)) {
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $payload $file)
        if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $thumbprint) { throw "Invalid $architecture signature: $file" }
    }
    foreach ($file in (@('AmtPtpDevice.inf') + $binaries)) {
        & $SignTool verify /q /pa /c (Join-Path $payload 'amtptpdevice.cat') (Join-Path $payload $file)
        if ($LASTEXITCODE -ne 0) { throw "$architecture catalog verification failed: $file" }
    }
    foreach ($file in @('AmtPtpHidFilter.inf','AmtPtpHidFilter.sys')) {
        & $SignTool verify /q /pa /c (Join-Path $payload 'amtptphidfilter.cat') (Join-Path $payload $file)
        if ($LASTEXITCODE -ne 0) { throw "$architecture standalone catalog verification failed: $file" }
    }
    $entries = foreach ($file in ($files + 'AmtPtpDeviceTest.cer')) {
        [pscustomobject]@{Name=$file; SHA256=(Get-FileHash -LiteralPath (Join-Path $payload $file)).Hash}
    }
    [pscustomobject]@{Version=$Version; SourceCommit=$revision; Architecture=$architecture; Signing='WDK test-signed'; CertificateThumbprint=$thumbprint; Files=@($entries)} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $payload 'manifest.json') -Encoding UTF8
    $archive = Join-Path $output "mac-precision-touchpad-v$Version-$architecture-driver.zip"
    $archiveFiles = @($files + 'AmtPtpDeviceTest.cer' + 'manifest.json' | ForEach-Object { Join-Path $payload $_ })
    $archiveFiles += Join-Path $PSScriptRoot 'INSTALL-DRIVER.txt'
    $archiveFiles += @('LICENSE-GPL.md','LICENSE-MIT.md','LICENSE.md' | ForEach-Object { Join-Path $root $_ })
    Compress-Archive -LiteralPath $archiveFiles -DestinationPath $archive -Force
    Write-Host "Verified $architecture binaries, signatures and catalogs."
    $archives += $archive
}
foreach ($file in @('Manage-Driver.ps1','DriverTools.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $common -Force
    & $SignTool sign /q /fd SHA256 /sha1 $thumbprint (Join-Path $common $file)
    if ($LASTEXITCODE -ne 0) { throw "Script signing failed: $file" }
}
foreach ($file in @('README.md','LICENSE-GPL.md','LICENSE-MIT.md','LICENSE.md')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $common -Force
}
Copy-Item -LiteralPath (Join-Path $root 'docs\installer.md') -Destination (Join-Path $common 'INSTALL.txt') -Force
$signCommand = '/Stouchpad=$q' + $SignTool + '$q sign /q /fd SHA256 /sha1 ' + $thumbprint + ' $f'
& $InnoCompiler "/DPackageRoot=$staging" "/DOutputRoot=$output" "/DAppVersion=$Version" $signCommand (Join-Path $PSScriptRoot 'Touchpad.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$exe = Join-Path $output "mac-precision-touchpad-v$Version-setup.exe"
$setupSignature = Get-AuthenticodeSignature -LiteralPath $exe
if ($setupSignature.Status -ne 'Valid' -or $setupSignature.SignerCertificate.Thumbprint -ne $thumbprint) {
    throw 'Installer signature verification failed.'
}
(@($exe) + $archives) | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant(), [IO.Path]::GetFileName($_)
} | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ASCII
Write-Host "Release artifacts: $output"
