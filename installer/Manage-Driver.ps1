param(
    [ValidateSet('Validate','Install','Uninstall')][string]$Action = 'Validate',
    [string]$Payload = (Join-Path $PSScriptRoot 'driver'),
    [switch]$TrustCertificate
)
$ErrorActionPreference = 'Stop'
# This runs elevated. Load modules only from the system location, never from a
# user's Documents folder, which -NoProfile leaves on the module path.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
. (Join-Path $PSScriptRoot 'DriverTools.ps1')
$exitCode = 0
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error 'Run Setup as administrator.'
    exit 1
}
$StateDirectory = Get-TrustedStateDirectory
$errorFile = Join-Path $StateDirectory 'last-error.txt'
$logPath = Join-Path $StateDirectory ($Action.ToLower() + '.log')
# Keep one previous log instead of growing without bound.
if ((Test-Path -LiteralPath $logPath) -and (Get-Item -LiteralPath $logPath).Length -gt 1MB) {
    Move-Item -LiteralPath $logPath -Destination "$logPath.old" -Force
}
Start-Transcript -Path $logPath -Append
try {
    if ($Action -eq 'Validate') {
        # Fail before files are copied, so a rejected install leaves nothing behind.
        Assert-Payload $Payload | Out-Null; Assert-TestSigning; Assert-CertificateTrust $TrustCertificate.IsPresent
    }
    else {
        if ($Action -eq 'Install') { $exitCode = Install-Touchpad $Payload $StateDirectory $TrustCertificate.IsPresent }
        else { $exitCode = Uninstall-Touchpad $StateDirectory }
    }
    if (Test-Path -LiteralPath $errorFile) { Remove-Item -LiteralPath $errorFile }
} catch {
    $_.Exception.Message | Set-Content -LiteralPath $errorFile -Encoding UTF8
    Write-Host $_.Exception.Message
    $exitCode = 1
} finally { Stop-Transcript }
exit $exitCode
