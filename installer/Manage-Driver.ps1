param(
    [ValidateSet('Validate','Install','Uninstall')][string]$Action = 'Validate',
    [string]$Payload = (Join-Path $PSScriptRoot 'driver'),
    [switch]$TrustCertificate
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DriverTools.ps1')
$exitCode = 0
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error 'Run Setup as administrator.'
    exit 1
}
$StateDirectory = Get-TrustedStateDirectory
$errorFile = Join-Path $StateDirectory 'last-error.txt'
Start-Transcript -Path (Join-Path $StateDirectory ($Action.ToLower() + '.log')) -Append
try {
    if ($Action -eq 'Validate') { Assert-Payload $Payload | Out-Null; Assert-TestSigning }
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
