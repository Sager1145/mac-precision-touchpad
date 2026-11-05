param(
    [string]$EwdkRoot = 'D:\BuildEnv',
    [ValidateSet('x64','ARM64')][string]$Platform = 'x64'
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$setup = Join-Path $EwdkRoot 'SetupBuildEnv.cmd'
if (-not (Test-Path -LiteralPath $setup)) { throw 'Supply -EwdkRoot pointing to an extracted/mounted Enterprise WDK build environment.' }
$output = Join-Path $repoRoot 'artifacts\build'
New-Item -ItemType Directory -Force $output | Out-Null
$command = @"
@echo off
call "$setup" amd64
if errorlevel 1 exit /b %errorlevel%
msbuild "$repoRoot\AmtPtpDriver.sln" /t:Rebuild /p:Configuration=Release /p:Platform=$Platform /p:WindowsTargetPlatformVersion=10.0.28000.0 /p:SkipPackageVerification=true /nologo /m:1 /v:normal
exit /b %errorlevel%
"@
$commandFile = Join-Path $output "build-release-$Platform.cmd"
[IO.File]::WriteAllText($commandFile, $command, [Text.Encoding]::ASCII)
# Windows PowerShell 5.1 turns redirected native stderr (vswhere, msbuild)
# into a terminating error under 'Stop'. Judge the build by its exit code.
$ErrorActionPreference = 'Continue'
& cmd.exe /c $commandFile 2>&1 | Tee-Object -FilePath (Join-Path $output "release-$Platform.log")
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -ne 0) { throw "Release $Platform build failed ($LASTEXITCODE)" }
