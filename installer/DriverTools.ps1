# Shared by the elevated installer entry point and its regression harness.
Set-StrictMode -Version Latest
$script:CertificateThumbprint = '94A13BEE28D788FC4A7FA91C568968C7DBADDC69'

function Get-TrustedStateDirectory {
    # ProgramData permits ordinary users to create directories. Keep elevated
    # logs and package ownership under the native protected Program Files root.
    $root = [Environment]::GetFolderPath('ProgramFiles')
    if (-not $root) { throw 'Cannot locate Program Files.' }
    $directory = Join-Path $root 'Mac Precision Touchpad State'
    $trusted = @('S-1-5-18','S-1-5-32-544',
        'S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464')
    $write = [Security.AccessControl.FileSystemRights]::Write -bor
        [Security.AccessControl.FileSystemRights]::Delete -bor
        [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles -bor
        [Security.AccessControl.FileSystemRights]::ChangePermissions -bor
        [Security.AccessControl.FileSystemRights]::TakeOwnership
    foreach ($path in @($root, $directory)) {
        if (-not (Test-Path -LiteralPath $path)) {
            New-Item -ItemType Directory -Path $path | Out-Null
        }
        $item = Get-Item -LiteralPath $path -Force
        if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Unsafe installer state directory: $path"
        }
        $acl = Get-Acl -LiteralPath $path
        if ($acl.GetOwner([Security.Principal.SecurityIdentifier]).Value -notin $trusted) {
            throw "Untrusted installer state owner: $path"
        }
        foreach ($rule in $acl.GetAccessRules($true, $true, [Security.Principal.SecurityIdentifier])) {
            if ($rule.AccessControlType -eq 'Allow' -and
                ($rule.FileSystemRights -band $write) -and $rule.IdentityReference.Value -notin $trusted -and
                $rule.IdentityReference.Value -ne 'S-1-3-0') {
                throw "Installer state is writable by an untrusted principal: $path"
            }
        }
    }
    # Logs record user and machine names. Program Files lets every account
    # read, so limit this folder to SYSTEM and Administrators. Only the access
    # list is written; the verified owner is left as it is.
    $protected = New-Object Security.AccessControl.DirectorySecurity
    $protected.SetAccessRuleProtection($true, $false)
    foreach ($sid in @('S-1-5-18','S-1-5-32-544')) {
        $protected.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule(
            (New-Object Security.Principal.SecurityIdentifier($sid)), 'FullControl',
            'ContainerInherit,ObjectInherit', 'None', 'Allow')))
    }
    (Get-Item -LiteralPath $directory -Force).SetAccessControl($protected)
    return $directory
}

function Assert-Payload([string]$Payload) {
    $manifest = Get-Content -LiteralPath (Join-Path $Payload 'manifest.json') -Raw | ConvertFrom-Json
    $native = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
    $expected = switch ($native) { 'AMD64' { 'x64' }; 'ARM64' { 'arm64' }; default { throw "Unsupported Windows architecture: $native" } }
    if ($manifest.Architecture -ne $expected) {
        throw "This package targets $($manifest.Architecture), but Windows requires $expected."
    }
    foreach ($file in $manifest.Files) {
        if ($file.Name -notmatch '^[a-zA-Z0-9_.-]+$') { throw 'Invalid payload filename.' }
        $path = Join-Path $Payload $file.Name
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.SHA256) {
            throw "Payload integrity check failed: $($file.Name)"
        }
    }
    foreach ($required in @('AmtPtpDevice.inf','amtptpdevice.cat','AmtPtpDeviceUsbUm.dll',
        'AmtPtpHidFilter.sys','AmtPtpDeviceUsbKm.sys','AmtPtpDeviceSpiKm.sys','AmtPtpBatteryService.exe',
        'AmtPtpDeviceTest.cer')) {
        if ($required -notin $manifest.Files.Name) { throw "Required payload missing: $required" }
    }
    $certificate = New-Object Security.Cryptography.X509Certificates.X509Certificate2(
        (Join-Path $Payload 'AmtPtpDeviceTest.cer'))
    if ($certificate.Thumbprint -ne $script:CertificateThumbprint) { throw 'Unexpected signing certificate.' }
    return $manifest
}

function Assert-TestSigning {
    # Microsoft SYSTEM_CODEINTEGRITY_INFORMATION, class 103, TESTSIGN bit 0x02.
    if (-not ('TouchpadInstaller.CodeIntegrity' -as [type])) {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
namespace TouchpadInstaller {
 public static class CodeIntegrity {
  [StructLayout(LayoutKind.Sequential)] public struct Information { public uint Length, Options; }
  [DllImport("ntdll.dll")] private static extern int NtQuerySystemInformation(
      int kind, ref Information info, uint length, out uint returned);
  public static uint Options() {
   Information info = new Information(); info.Length = 8; uint returned;
   int status = NtQuerySystemInformation(103, ref info, 8, out returned);
   if (status < 0) throw new InvalidOperationException("Unable to read Code Integrity status: " + status);
   return info.Options;
  }
 }
}
'@
    }
    if (([TouchpadInstaller.CodeIntegrity]::Options() -band 2) -eq 0) {
        throw 'Windows is not currently allowing test-signed drivers. See the bundled installation guide, enable test signing yourself, reboot, then run Setup again. Setup does not change boot or Secure Boot settings.'
    }
}

function Assert-CertificateTrust([bool]$TrustCertificate) {
    if (-not (Test-CertificateTrusted) -and -not $TrustCertificate) {
        throw 'The bundled test certificate is not trusted. Select the certificate trust task in Setup, or install its public certificate manually as described in the guide.'
    }
}

function Invoke-PnpUtil([string[]]$Arguments) {
    # Windows PowerShell 5.1 turns redirected native stderr into a terminating
    # error under 'Stop'. Judge pnputil by its exit code instead.
    $ErrorActionPreference = 'Continue'
    $output = & "$env:WINDIR\System32\pnputil.exe" @Arguments 2>&1
    $code = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    if ($code -notin @(0,259,3010)) { throw "PnPUtil failed with exit code $code. See the installer log." }
    return $code
}

function Find-PublishedPackage([string]$Payload) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $Payload 'AmtPtpDevice.inf')).Hash
    # Compare exact INF contents, independent of localized pnputil output.
    return @(Get-ChildItem -LiteralPath "$env:WINDIR\INF" -Filter 'oem*.inf' | Where-Object {
        (Get-FileHash -LiteralPath $_.FullName).Hash -eq $hash
    } | ForEach-Object { [pscustomobject]@{Name=$_.Name; SHA256=$hash} })
}

function Test-ProjectPackage([object[]]$Drivers, [string]$Name) {
    $driver = @($Drivers | Where-Object { $_.Driver -eq $Name }) | Select-Object -First 1
    if (-not $driver -or [IO.Path]::GetFileName($driver.OriginalFileName) -ne 'amtptpdevice.inf') { return $false }
    $catalog = Join-Path (Split-Path -Parent $driver.OriginalFileName) 'amtptpdevice.cat'
    if (-not (Test-Path -LiteralPath $catalog)) { return $false }
    $signature = Get-AuthenticodeSignature -LiteralPath $catalog
    return ($signature.Status -eq 'Valid' -and
        $signature.SignerCertificate.Thumbprint -eq $script:CertificateThumbprint)
}

function Get-RecordValue([object]$Object, [string]$Name) {
    # Ownership records may be old, partial or hand-made. Read fields without
    # tripping strict mode on missing properties.
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($property) { return $property.Value }
    return $null
}

function Get-LegacyOwnedPackages([object[]]$Drivers) {
    # Preview builds recorded ownership under ProgramData, which ordinary users
    # can write. Adopt an entry only while it still matches its recorded hash
    # and is a driver-store package signed by this project's certificate. A
    # malformed record is ignored rather than allowed to fail Setup.
    $legacy = Join-Path $env:ProgramData 'Sager1145\MacPrecisionTouchpad\installed.json'
    if (-not (Test-Path -LiteralPath $legacy)) { return @() }
    try {
        $record = Get-Content -LiteralPath $legacy -Raw | ConvertFrom-Json
        $adopted = @()
        foreach ($entry in @(Get-RecordValue $record 'Packages')) {
            $name = [string](Get-RecordValue $entry 'Name')
            $hash = [string](Get-RecordValue $entry 'SHA256')
            if ($name -notmatch '^oem[0-9]+\.inf$' -or -not $hash) { continue }
            $inf = Join-Path "$env:WINDIR\INF" $name
            if ((Test-Path -LiteralPath $inf) -and (Get-FileHash -LiteralPath $inf).Hash -eq $hash -and
                (Test-ProjectPackage $Drivers $name)) {
                $adopted += [pscustomobject]@{Name=$name; SHA256=$hash}
            }
        }
        return $adopted
    } catch {
        Write-Host "Ignoring unreadable preview ownership record: $($_.Exception.Message)"
        return @()
    }
}

function Test-CertificateTrusted {
    $thumb = $script:CertificateThumbprint
    return ((Test-Path "Cert:\LocalMachine\Root\$thumb") -and
        (Test-Path "Cert:\LocalMachine\TrustedPublisher\$thumb"))
}

function Remove-TrustedCertificate {
    foreach ($store in @('Root','TrustedPublisher')) {
        $path = "Cert:\LocalMachine\$store\$($script:CertificateThumbprint)"
        if (Test-Path $path) { Remove-Item -LiteralPath $path }
    }
}

function Save-Ownership([string]$StateDirectory, [object]$State) {
    $statePath = Join-Path $StateDirectory 'installed.json'
    $temporaryState = Join-Path $StateDirectory ([guid]::NewGuid().ToString('N') + '.json')
    $State | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $temporaryState -Encoding UTF8
    if ([IO.File]::Exists($statePath)) { [IO.File]::Replace($temporaryState, $statePath, [NullString]::Value) }
    else { [IO.File]::Move($temporaryState, $statePath) }
}

function Install-Touchpad([string]$Payload, [string]$StateDirectory, [bool]$TrustCertificate) {
    $manifest = Assert-Payload $Payload
    Assert-TestSigning
    $thumb = $script:CertificateThumbprint
    Assert-CertificateTrust $TrustCertificate
    $statePath = Join-Path $StateDirectory 'installed.json'
    $old = $null
    if (Test-Path -LiteralPath $statePath) { $old = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json }
    $certificateImported = [bool](Get-RecordValue $old 'CertificateImported')
    $alreadyStaged = @()
    $addAttempted = $false
    $published = @()
    $drivers = $null
    $backup = $null
    $code = 0
    if ($TrustCertificate) {
        # Only trust that Setup itself added is removed again at uninstall.
        $wasTrusted = Test-CertificateTrusted
        foreach ($store in @('Root','TrustedPublisher')) {
            Import-Certificate -FilePath (Join-Path $Payload 'AmtPtpDeviceTest.cer') -CertStoreLocation "Cert:\LocalMachine\$store" | Out-Null
        }
        if (-not $wasTrusted) { $certificateImported = $true }
    }
    try {
        foreach ($file in @('amtptpdevice.cat','AmtPtpDeviceUsbUm.dll','AmtPtpHidFilter.sys',
            'AmtPtpDeviceUsbKm.sys','AmtPtpDeviceSpiKm.sys','AmtPtpBatteryService.exe')) {
            $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $Payload $file)
            if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $thumb) {
                throw "Invalid driver signature: $file"
            }
        }
        $backup = Join-Path $StateDirectory ('backup-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
        New-Item -ItemType Directory -Force $backup | Out-Null
        # Preserve any existing driver from this family before rebinding devices.
        $drivers = @(Get-WindowsDriver -Online -All)
        $previous = @($drivers | Where-Object {
            [IO.Path]::GetFileName($_.OriginalFileName) -in @('amtptpdevice.inf','amtptphidfilter.inf')
        })
        foreach ($driver in $previous) { Invoke-PnpUtil @('/export-driver', $driver.Driver, $backup) | Out-Null }
        $alreadyStaged = @(Find-PublishedPackage $Payload)
        $addAttempted = $true
        $code = Invoke-PnpUtil @('/add-driver', (Join-Path $Payload 'AmtPtpDevice.inf'), '/install')
    } finally {
        # Record what this run added even when a step failed, so uninstall can
        # still remove a package pnputil staged before reporting an error.
        $owned = @()
        if ($addAttempted) {
            $published = @(Find-PublishedPackage $Payload)
            $owned = @($published | Where-Object { $_.Name -notin @($alreadyStaged | ForEach-Object Name) })
        }
        $owned += @(@(Get-RecordValue $old 'Packages') | Where-Object {
            $_ -and (Get-RecordValue $_ 'Name') -and $_.Name -notin @($owned | ForEach-Object Name) })
        if ($drivers) {
            $owned += @(Get-LegacyOwnedPackages $drivers | Where-Object { $_.Name -notin @($owned | ForEach-Object Name) })
        }
        if ($owned.Count -or $certificateImported -or $old) {
            Save-Ownership $StateDirectory ([pscustomobject]@{Version=$manifest.Version; Packages=$owned;
                CertificateImported=$certificateImported; Backup=$backup; RebootRequired=($code -eq 3010)})
        }
    }
    if (-not $published.Count) { throw 'The signed driver package was not found in the Windows driver store after installation.' }
    try { Install-BatteryService $Payload }
    catch {
        # Battery display is optional; never fail the driver install over it.
        Write-Host "Battery service was not installed: $($_.Exception.Message)"
    }
    Write-Host "Driver staged/installed. Reconnect an unplugged trackpad to bind the update. Previous drivers: $backup"
    if ($code -eq 3010) { return 3010 }
    return 0
}

$script:BatteryServiceName = 'AmtPtpBattery'

function Install-BatteryService([string]$Payload) {
    # The service publishes the Bluetooth battery level for Windows Settings.
    # Writing a device property needs administrator rights, so it runs as
    # LocalSystem, from the protected Program Files payload folder.
    $exe = Join-Path $Payload 'AmtPtpBatteryService.exe'
    $name = $script:BatteryServiceName
    $sc = "$env:WINDIR\System32\sc.exe"
    $ErrorActionPreference = 'Stop'
    $existing = Get-Service -Name $name -ErrorAction SilentlyContinue
    if ($existing) {
        if ($existing.Status -ne 'Stopped') { Stop-Service -Name $name -Force }
        # The binary path is fixed, but keep it exact after an upgrade.
        Set-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Services\$name" -Name ImagePath -Value "`"$exe`""
        Set-Service -Name $name -StartupType Automatic
    } else {
        New-Service -Name $name -BinaryPathName "`"$exe`"" -DisplayName 'Mac Precision Touchpad battery level' `
            -Description 'Shows the Magic Trackpad Bluetooth battery level in Windows Settings.' -StartupType Automatic | Out-Null
    }
    # Keep only the privilege every service needs. Administrator group
    # membership is what allows the device property write.
    & $sc privs $name SeChangeNotifyPrivilege | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "sc privs failed with exit code $LASTEXITCODE" }
    & $sc failure $name reset= 86400 actions= restart/60000/restart/60000// | Out-Null
    Start-Service -Name $name
}

function Remove-BatteryService {
    $name = $script:BatteryServiceName
    $existing = Get-Service -Name $name -ErrorAction SilentlyContinue
    if (-not $existing) { return }
    if ($existing.Status -ne 'Stopped') { Stop-Service -Name $name -Force -ErrorAction SilentlyContinue }
    & "$env:WINDIR\System32\sc.exe" delete $name | Out-Null
}

function Uninstall-Touchpad([string]$StateDirectory) {
    # The service is ours by name and runs from this app's folder, so remove
    # it whether or not an ownership record exists.
    Remove-BatteryService
    $path = Join-Path $StateDirectory 'installed.json'
    if (-not (Test-Path -LiteralPath $path)) {
        # Nothing is recorded as ours, so there is nothing to remove. Do not
        # block removal of the application itself.
        Write-Host 'No installation ownership record; no drivers were removed.'
        return 0
    }
    $state = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    # Validate every entry before deleting any package. Never remove by device
    # vendor wildcard, never force-delete, and never touch another fork's INF.
    $remove = @()
    foreach ($package in @(Get-RecordValue $state 'Packages')) {
        if ([string](Get-RecordValue $package 'Name') -notmatch '^oem[0-9]+\.inf$') { throw 'Invalid published INF in ownership record.' }
        $inf = Join-Path "$env:WINDIR\INF" $package.Name
        if (-not (Test-Path -LiteralPath $inf)) { continue }
        if ((Get-FileHash -LiteralPath $inf).Hash -ne [string](Get-RecordValue $package 'SHA256')) {
            # Windows reuses freed oemNN names. This one now belongs elsewhere.
            Write-Host "Skipping $($package.Name): it no longer matches the installed package."
            continue
        }
        $remove += $package
    }
    $reboot = 0
    foreach ($package in $remove) {
        if (Test-Path -LiteralPath (Join-Path "$env:WINDIR\INF" $package.Name)) {
            $code = Invoke-PnpUtil @('/delete-driver', $package.Name, '/uninstall')
            if ($code -eq 3010) { $reboot = 3010 }
        }
    }
    # Undo certificate trust only when this installer added it. Trust that was
    # present before Setup ran belongs to someone else and stays.
    if ([bool](Get-RecordValue $state 'CertificateImported')) { Remove-TrustedCertificate }
    Remove-Item -LiteralPath $path
    # Keep backups and logs for recovery.
    return $reboot
}
