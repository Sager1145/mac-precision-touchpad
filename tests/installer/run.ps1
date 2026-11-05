$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
. (Join-Path $root 'installer\DriverTools.ps1')
$fixture = Join-Path $root ('artifacts\installer-tests\' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $fixture 'driver'
$state = Join-Path $fixture 'state'
$fakeWindows = Join-Path $fixture 'Windows'
New-Item -ItemType Directory -Force $payload,$state,(Join-Path $fakeWindows 'INF') | Out-Null
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Rejects([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
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
$env:WINDIR = $fakeWindows
$script:calls = New-Object 'Collections.Generic.List[string]'
$script:failPnp = $false
$script:addDriverCode = 0
$script:imports = 0
function Assert-TestSigning { }
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
function Get-WindowsDriver { @([pscustomobject]@{OriginalFileName='C:\old\amtptpdevice.inf';Driver='oem4.inf'}) }
function Invoke-PnpUtil([string[]]$Arguments) {
    $script:calls.Add(($Arguments -join ' '))
    if ($script:failPnp) { throw 'Injected PnP failure' }
    if ($Arguments[0] -eq '/add-driver') {
        if ($script:addDriverCode -eq 0) {
            Copy-Item -LiteralPath $Arguments[1] -Destination (Join-Path $fakeWindows 'INF\oem42.inf')
        }
        return $script:addDriverCode
    }
    return 0
}
try {
    Rejects { Install-Touchpad $payload $state $false } 'Unapproved certificate trust accepted'
    Assert ($imports -eq 0 -and $calls.Count -eq 0) 'Unapproved trust mutated state'
    Install-Touchpad $payload $state $true | Out-Null
    Assert ($imports -eq 2) 'Both explicitly approved trust stores were not used'
    Assert ($calls[0] -like '/export-driver oem4.inf *') 'Previous driver not backed up first'
    Assert ($calls[1] -like '/add-driver */install') 'Install command incorrect'
    $record = Join-Path $state 'installed.json'
    $owned = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    Assert ($owned.Packages[0].Name -eq 'oem42.inf') 'Incorrect package ownership'
    Copy-Item -LiteralPath (Join-Path $payload 'AmtPtpDevice.inf') -Destination (Join-Path $fakeWindows 'INF\oem43.inf')
    $script:addDriverCode = 259
    Assert ((Install-Touchpad $payload $state $true) -eq 0) 'An already-staged matching package should report success'
    $owned = Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
    Assert ($owned.Packages.Count -eq 1 -and $owned.Packages[0].Name -eq 'oem42.inf') 'Setup adopted an identical manually staged package'
    $script:addDriverCode = 0
    $script:calls.Clear()
    Add-Content -LiteralPath (Join-Path $fakeWindows 'INF\oem42.inf') 'another owner'
    Rejects { Uninstall-Touchpad $state } 'Changed ownership was removed'
    Assert ($calls.Count -eq 0) 'Ownership rejection still removed drivers'
    Copy-Item -LiteralPath (Join-Path $payload 'AmtPtpDevice.inf') -Destination (Join-Path $fakeWindows 'INF\oem42.inf') -Force
    $saved = Get-Content -LiteralPath $record -Raw
    $owned.Packages[0].Name = '..\other.inf'
    $owned | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $record
    Rejects { Uninstall-Touchpad $state } 'Escaping uninstall path accepted'
    Assert ($calls.Count -eq 0) 'Invalid ownership record removed drivers'
    Set-Content -LiteralPath $record $saved
    $script:failPnp = $true
    Rejects { Uninstall-Touchpad $state } 'PnP failure was reported as success'
    Assert (Test-Path -LiteralPath $record) 'Failure destroyed recovery record'
    $script:failPnp = $false
    $script:calls.Clear()
    Uninstall-Touchpad $state | Out-Null
    Assert ($calls.Count -eq 1 -and $calls[0] -eq '/delete-driver oem42.inf /uninstall') 'Uninstall touched unexpected packages or force-deleted'
    Assert (-not (Test-Path -LiteralPath $record)) 'Successful uninstall retained ownership record'
    Rejects { Uninstall-Touchpad $state } 'Unowned uninstall accepted'
    Write-Host 'PASS: installer payload tamper/missing/path validation, explicit certificate consent, backup-before-install, exact ownership, uninstall failures and recovery records'
} finally { $env:WINDIR = $originalWindows }
