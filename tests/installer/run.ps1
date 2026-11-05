$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'installer\DriverTools.ps1')
$fixture = Join-Path $root ('artifacts\installer-tests\' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $fixture 'driver'
$state = Join-Path $fixture 'state'
$fakeWindows = Join-Path $fixture 'Windows'
New-Item -ItemType Directory -Force $payload,$state,(Join-Path $fakeWindows 'INF') | Out-Null
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Rejects([scriptblock]$Action, [string]$Message, [string]$Expected = '') {
    $rejected = $false
    try { & $Action } catch {
        # A typo or missing fixture must not count as the expected rejection.
        if ($Expected -and $_.Exception.Message -notlike "*$Expected*") { throw "$Message (unexpected error: $($_.Exception.Message))" }
        $rejected = $true
    }
    Assert $rejected $Message
}
$required = @('AmtPtpDevice.inf','amtptpdevice.cat','AmtPtpDeviceUsbUm.dll',
    'AmtPtpHidFilter.sys','AmtPtpDeviceUsbKm.sys','AmtPtpDeviceSpiKm.sys')
foreach ($file in $required) { Set-Content -LiteralPath (Join-Path $payload $file) $file }
Copy-Item -LiteralPath (Join-Path $root 'build\AmtPtpDeviceUniversalPkg\x64\Release\AmtPtpDeviceUniversalPkg.cer') `
    -Destination (Join-Path $payload 'AmtPtpDeviceTest.cer')
$native = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
$architecture = if ($native -eq 'ARM64') { 'arm64' } else { 'x64' }
$manifest = [pscustomobject]@{Version='test'; Architecture=$architecture; Files=@(Get-ChildItem -LiteralPath $payload -File | ForEach-Object {
    [pscustomobject]@{Name=$_.Name; SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})}
$manifestPath = Join-Path $payload 'manifest.json'
function Save-Manifest { $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath }
Save-Manifest
Assert ((Assert-Payload $payload).Version -eq 'test') 'Valid payload rejected'
$manifest.Architecture = if ($architecture -eq 'arm64') { 'x64' } else { 'arm64' }
Save-Manifest
Rejects { Assert-Payload $payload } 'Wrong architecture accepted'
$manifest.Architecture = $architecture
Save-Manifest
Add-Content -LiteralPath (Join-Path $payload 'AmtPtpHidFilter.sys') 'tampered'
Rejects { Assert-Payload $payload } 'Tampered driver accepted'
Set-Content -LiteralPath (Join-Path $payload 'AmtPtpHidFilter.sys') 'AmtPtpHidFilter.sys'
$originalFiles = $manifest.Files
$manifest.Files = @($manifest.Files | Where-Object Name -ne 'AmtPtpDevice.inf')
Save-Manifest
Rejects { Assert-Payload $payload } 'Missing required driver accepted'
$manifest.Files = @([pscustomobject]@{Name='..\outside';SHA256='unused'})
Save-Manifest
Rejects { Assert-Payload $payload } 'Escaping payload filename accepted'
$manifest.Files = $originalFiles
Save-Manifest

# From here all Windows mutation APIs are mocked and WINDIR points at fixtures.
# No actual drivers, certificates or boot options are modified by this suite.
$originalWindows = $env:WINDIR
$originalProgramData = $env:ProgramData
$env:WINDIR = $fakeWindows
$env:ProgramData = Join-Path $fixture 'ProgramData'
# A preview-era record: oem7 is a package signed by this project, oem8 is not
# a project package, and oem9 has changed since it was recorded.
$repository = Join-Path $fakeWindows 'System32\DriverStore\FileRepository\amtptpdevice.inf_test'
New-Item -ItemType Directory -Force $repository,(Join-Path $env:ProgramData 'Sager1145\MacPrecisionTouchpad') | Out-Null
Set-Content -LiteralPath (Join-Path $repository 'amtptpdevice.cat') 'catalog'
foreach ($name in 'oem7.inf','oem8.inf','oem9.inf') { Set-Content -LiteralPath (Join-Path $fakeWindows "INF\$name") $name }
$legacyPackages = @('oem7.inf','oem8.inf','oem9.inf') | ForEach-Object {
    [pscustomobject]@{Name=$_; SHA256=(Get-FileHash -LiteralPath (Join-Path $fakeWindows "INF\$_")).Hash}
}
[pscustomobject]@{Packages=$legacyPackages} | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $env:ProgramData 'Sager1145\MacPrecisionTouchpad\installed.json')
Add-Content -LiteralPath (Join-Path $fakeWindows 'INF\oem9.inf') 'changed'
$script:calls = New-Object 'Collections.Generic.List[string]'
$script:failPnp = $false
$script:addDriverCode = 0
$script:imports = 0
$script:certRemovals = 0
$script:failAfterStaging = $false
function Assert-TestSigning { }
function Remove-TrustedCertificate { $script:certRemovals++ }
function Test-Path {
    param([Parameter(Position=0)][string]$Path,[string]$LiteralPath,[string]$PathType)
    $target = if ($LiteralPath) { $LiteralPath } else { $Path }
    if ($target -like 'Cert:*') { return $false }
    Microsoft.PowerShell.Management\Test-Path @PSBoundParameters
}
function Import-Certificate { $script:imports++ }
function Get-AuthenticodeSignature {
    [pscustomobject]@{Status='Valid';SignerCertificate=[pscustomobject]@{Thumbprint=$script:CertificateThumbprint}}
}
function Get-WindowsDriver { @(
    [pscustomobject]@{OriginalFileName='C:\old\amtptpdevice.inf';Driver='oem4.inf'},
    [pscustomobject]@{OriginalFileName=(Join-Path $repository 'amtptpdevice.inf');Driver='oem7.inf'},
    [pscustomobject]@{OriginalFileName=(Join-Path $repository 'amtptpdevice.inf');Driver='oem9.inf'}
) }
function Invoke-PnpUtil([string[]]$Arguments) {
    $script:calls.Add(($Arguments -join ' '))
    if ($script:failPnp) { throw 'Injected PnP failure' }
    if ($Arguments[0] -eq '/add-driver') {
        if ($script:addDriverCode -eq 0) {
            Copy-Item -LiteralPath $Arguments[1] -Destination (Join-Path $fakeWindows 'INF\oem42.inf')
        }
        # pnputil can stage a package and then fail to install it on a device.
        if ($script:failAfterStaging) { throw 'Injected failure after staging' }
        return $script:addDriverCode
    }
    return 0
}
try {
    # Any user can create the preview-era record. Malformed ones must be ignored.
    $legacyRecord = Join-Path $env:ProgramData 'Sager1145\MacPrecisionTouchpad\installed.json'
    $legacyText = Get-Content -LiteralPath $legacyRecord -Raw
    foreach ($bad in '{}', '{"Packages":[{"Name":"oem7.inf"}]}', '{"Packages":["oem7.inf", null, 5]}', 'not json') {
        Set-Content -LiteralPath $legacyRecord $bad
        Assert (@(Get-LegacyOwnedPackages (Get-WindowsDriver)).Count -eq 0) "Malformed preview record was adopted: $bad"
    }
    Set-Content -LiteralPath $legacyRecord $legacyText

    # A pnputil failure after staging must still record the staged package.
    $failState = Join-Path $fixture 'fail-state'
    New-Item -ItemType Directory -Force $failState | Out-Null
    $script:failAfterStaging = $true
    Rejects { Install-Touchpad $payload $failState $true } 'Staging failure was reported as success' 'Injected failure after staging'
    $script:failAfterStaging = $false
    $failRecord = Get-Content -LiteralPath (Join-Path $failState 'installed.json') -Raw | ConvertFrom-Json
    Assert ('oem42.inf' -in @($failRecord.Packages | ForEach-Object Name)) 'Failed install did not record the staged package'
    Assert ($failRecord.CertificateImported) 'Failed install did not record certificate trust it added'
    Remove-Item -LiteralPath (Join-Path $fakeWindows 'INF\oem42.inf')
    $script:calls.Clear()
    $script:imports = 0

    Rejects { Assert-CertificateTrust $false } 'Validation accepted an untrusted certificate' 'not trusted'
    Assert-CertificateTrust $true
    Rejects { Install-Touchpad $payload $state $false } 'Unapproved certificate trust accepted' 'not trusted'
    Assert ($imports -eq 0 -and $calls.Count -eq 0) 'Unapproved trust mutated state'
    Install-Touchpad $payload $state $true | Out-Null
    Assert ($imports -eq 2) 'Both explicitly approved trust stores were not used'
    Assert ($calls[0] -like '/export-driver oem4.inf *') 'Previous driver not backed up first'
    Assert ($calls[-1] -like '/add-driver */install') 'Install command incorrect'
    $record = Join-Path $state 'installed.json'
    $owned = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    Assert ($owned.Packages[0].Name -eq 'oem42.inf') 'Incorrect package ownership'
    Assert ($owned.CertificateImported) 'Certificate trust added by Setup was not recorded'
    $ownedNames = @($owned.Packages | ForEach-Object Name)
    Assert ('oem7.inf' -in $ownedNames) 'Signed preview-era package was not adopted'
    Assert ('oem8.inf' -notin $ownedNames -and 'oem9.inf' -notin $ownedNames) 'Unsigned or changed preview-era package was adopted'
    Copy-Item -LiteralPath (Join-Path $payload 'AmtPtpDevice.inf') -Destination (Join-Path $fakeWindows 'INF\oem43.inf')
    $script:addDriverCode = 259
    Assert ((Install-Touchpad $payload $state $true) -eq 0) 'An already-staged matching package should report success'
    $owned = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    Assert ($owned.Packages.Count -eq 2 -and 'oem43.inf' -notin @($owned.Packages | ForEach-Object Name)) 'Setup adopted an identical manually staged package'
    $script:addDriverCode = 0
    $saved = Get-Content -LiteralPath $record -Raw
    $script:calls.Clear()
    # A reused oemNN name now holds someone else's package: skip it, remove the rest.
    Add-Content -LiteralPath (Join-Path $fakeWindows 'INF\oem42.inf') 'another owner'
    Assert ((Uninstall-Touchpad $state) -eq 0) 'Reused package name blocked uninstall'
    Assert ($calls.Count -eq 1 -and $calls[0] -eq '/delete-driver oem7.inf /uninstall') 'Changed ownership was removed'
    Copy-Item -LiteralPath (Join-Path $payload 'AmtPtpDevice.inf') -Destination (Join-Path $fakeWindows 'INF\oem42.inf') -Force
    Set-Content -LiteralPath $record $saved
    $script:calls.Clear()
    $owned.Packages[0].Name = '..\other.inf'
    $owned | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $record
    Rejects { Uninstall-Touchpad $state } 'Escaping uninstall path accepted' 'Invalid published INF'
    Assert ($calls.Count -eq 0) 'Invalid ownership record removed drivers'
    Set-Content -LiteralPath $record $saved
    $script:failPnp = $true
    Rejects { Uninstall-Touchpad $state } 'PnP failure was reported as success' 'Injected PnP failure'
    Assert (Test-Path -LiteralPath $record) 'Failure destroyed recovery record'
    $script:failPnp = $false
    $script:calls.Clear()
    Uninstall-Touchpad $state | Out-Null
    Assert ($calls.Count -eq 2 -and $calls[0] -eq '/delete-driver oem42.inf /uninstall' -and
        $calls[1] -eq '/delete-driver oem7.inf /uninstall') 'Uninstall touched unexpected packages or force-deleted'
    Assert (-not (Test-Path -LiteralPath $record)) 'Successful uninstall retained ownership record'
    Assert ($script:certRemovals -gt 0) 'Uninstall kept certificate trust that Setup added'
    # Trust that existed before Setup is never recorded, so it is never removed.
    $script:certRemovals = 0
    [pscustomobject]@{Packages=@(); CertificateImported=$false} | ConvertTo-Json | Set-Content -LiteralPath $record
    Uninstall-Touchpad $state | Out-Null
    Assert ($script:certRemovals -eq 0) 'Uninstall removed certificate trust it did not add'
    $script:calls.Clear()
    Assert ((Uninstall-Touchpad $state) -eq 0 -and $calls.Count -eq 0) 'Missing ownership record blocked uninstall or removed drivers'
    Write-Host 'PASS: installer payload tamper/missing/path validation, explicit certificate consent, backup-before-install, exact ownership, preview record adoption, reused package names, uninstall failures and recovery records'
} finally { $env:WINDIR = $originalWindows; $env:ProgramData = $originalProgramData }
