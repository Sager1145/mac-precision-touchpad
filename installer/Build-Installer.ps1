param(
    [string]$Version = '0.2.0-preview.1',
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
$payload = Join-Path $staging 'driver'
New-Item -ItemType Directory -Force $payload | Out-Null
$thumbprint = '94A13BEE28D788FC4A7FA91C568968C7DBADDC69'
foreach ($tool in @($InnoCompiler,$SignTool)) { if (-not (Test-Path -LiteralPath $tool)) { throw "Build tool missing: $tool" } }
$built = Join-Path $root 'build\AmtPtpDeviceUniversalPkg\x64\Release\AmtPtpDeviceUniversalPkg'
$driverFiles = @('AmtPtpDevice.inf','amtptpdevice.cat','AmtPtpDeviceUsbUm.dll',
    'AmtPtpHidFilter.sys','AmtPtpDeviceUsbKm.sys','AmtPtpDeviceSpiKm.sys',
    'AmtPtpHidFilter.inf','amtptphidfilter.cat')
foreach ($file in $driverFiles) { Copy-Item -LiteralPath (Join-Path $built $file) -Destination $payload -Force }
Copy-Item -LiteralPath (Join-Path (Split-Path $built) 'AmtPtpDeviceUniversalPkg.cer') -Destination (Join-Path $payload 'AmtPtpDeviceTest.cer') -Force
$cert = New-Object Security.Cryptography.X509Certificates.X509Certificate2((Join-Path $payload 'AmtPtpDeviceTest.cer'))
if ($cert.Thumbprint -ne $thumbprint) { throw 'The build used an unexpected signing certificate.' }
foreach ($file in @('amtptpdevice.cat','amtptphidfilter.cat','AmtPtpDeviceUsbUm.dll',
    'AmtPtpHidFilter.sys','AmtPtpDeviceUsbKm.sys','AmtPtpDeviceSpiKm.sys')) {
    $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $payload $file)
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $thumbprint) { throw "Invalid signature: $file" }
}
foreach ($file in @('AmtPtpDevice.inf','AmtPtpDeviceUsbUm.dll','AmtPtpHidFilter.sys','AmtPtpDeviceUsbKm.sys','AmtPtpDeviceSpiKm.sys')) {
    & $SignTool verify /q /pa /c (Join-Path $payload 'amtptpdevice.cat') (Join-Path $payload $file)
    if ($LASTEXITCODE -ne 0) { throw "Catalog verification failed: $file" }
}
& $SignTool verify /q /pa /c (Join-Path $payload 'amtptphidfilter.cat') (Join-Path $payload 'AmtPtpHidFilter.inf')
if ($LASTEXITCODE -ne 0) { throw 'Standalone Bluetooth catalog verification failed.' }
$revision = & git -C $root rev-parse HEAD
$files = @(Get-ChildItem -LiteralPath $payload -File | Sort-Object Name | ForEach-Object {
    [pscustomobject]@{Name=$_.Name; SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
[pscustomobject]@{Version=$Version; SourceCommit=$revision; Architecture='x64'; Signing='WDK test-signed'; CertificateThumbprint=$thumbprint; Files=$files} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $payload 'manifest.json') -Encoding UTF8
foreach ($file in @('Manage-Driver.ps1','DriverTools.ps1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $staging -Force
    & $SignTool sign /q /fd SHA256 /sha1 $thumbprint (Join-Path $staging $file)
    if ($LASTEXITCODE -ne 0) { throw "Script signing failed: $file" }
}
foreach ($file in @('README.md','LICENSE-GPL.md','LICENSE-MIT.md','LICENSE.md')) {
    Copy-Item -LiteralPath (Join-Path $root $file) -Destination $staging -Force
}
Copy-Item -LiteralPath (Join-Path $root 'docs\installer.md') -Destination (Join-Path $staging 'INSTALL.txt') -Force
$signCommand = '/Stouchpad=$q' + $SignTool + '$q sign /q /fd SHA256 /sha1 ' + $thumbprint + ' $f'
& $InnoCompiler "/DPackageRoot=$staging" "/DOutputRoot=$output" "/DAppVersion=$Version" $signCommand (Join-Path $PSScriptRoot 'Touchpad.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$exe = Join-Path $output "mac-precision-touchpad-v$Version-x64-setup.exe"
if ((Get-AuthenticodeSignature -LiteralPath $exe).Status -ne 'Valid') { throw 'Installer signature verification failed.' }
$zip = Join-Path $output "mac-precision-touchpad-v$Version-x64-test-signed.zip"
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip -Force
@($exe,$zip) | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_).Hash.ToLowerInvariant(), [IO.Path]::GetFileName($_)
} | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ASCII
Write-Host "Release artifacts: $output"
