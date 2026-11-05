#ifndef PackageRoot
  #error PackageRoot must point to the prepared release directory
#endif
#ifndef AppVersion
  #define AppVersion "0.2.2"
#endif
[Setup]
AppId={{B9DA7434-8AF2-4F63-84D8-179566B48BD5}
AppName=Mac Precision Touchpad (Sager1145)
AppVersion={#AppVersion}
VersionInfoVersion=0.2.2.0
AppPublisher=Sager1145
AppPublisherURL=https://github.com/Sager1145/mac-precision-touchpad
AppSupportURL=https://github.com/Sager1145/mac-precision-touchpad/issues
AppUpdatesURL=https://github.com/Sager1145/mac-precision-touchpad/releases
DefaultDirName={autopf}\Mac Precision Touchpad
; The installed scripts run elevated at install and uninstall. Keep them in
; protected Program Files rather than a folder ordinary users could modify.
DisableDirPage=yes
UsePreviousAppDir=no
DefaultGroupName=Mac Precision Touchpad
DisableProgramGroupPage=yes
DisableWelcomePage=no
WizardStyle=modern
ArchitecturesAllowed=x64os or arm64
ArchitecturesInstallIn64BitMode=x64os or arm64
MinVersion=10.0.17134
PrivilegesRequired=admin
LicenseFile={#PackageRoot}\common\LICENSE-GPL.md
InfoBeforeFile=notice.txt
OutputDir={#OutputRoot}
OutputBaseFilename=mac-precision-touchpad-v{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
SetupLogging=yes
CloseApplications=no
RestartApplications=no
UninstallDisplayName=Mac Precision Touchpad (Sager1145)
UninstallDisplayIcon={sys}\main.cpl
SignTool=touchpad
SignedUninstaller=yes

[Messages]
ConfirmUninstall=Remove %1 and the driver packages this installer added?%n%nThe trackpad may disconnect briefly, so keep another input device available. Backups and logs are retained. Certificate trust is removed only if this installer added it.

[Tasks]
Name: trustcert; Description: "Trust the bundled WDK test certificate. Windows will then trust any software signed with it, not only this driver, until you remove it or uninstall this app"; Flags: unchecked

[Files]
Source: "{#PackageRoot}\common\*"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageRoot}\common\*.ps1"; DestDir: "{tmp}"; Flags: dontcopy
Source: "{#PackageRoot}\x64\*"; DestDir: "{app}\driver"; Flags: ignoreversion; Check: IsX64OS
Source: "{#PackageRoot}\arm64\*"; DestDir: "{app}\driver"; Flags: ignoreversion; Check: IsArm64
Source: "{#PackageRoot}\x64\*"; DestDir: "{tmp}\x64"; Flags: dontcopy
Source: "{#PackageRoot}\arm64\*"; DestDir: "{tmp}\arm64"; Flags: dontcopy

[Icons]
Name: "{group}\Touchpad settings"; Filename: "ms-settings:devices-touchpad"
Name: "{group}\Installation guide"; Filename: "{app}\INSTALL.txt"
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"

[Code]
var
  RebootRequired: Boolean;

function RunDriverTool(Action, ScriptPath, Payload: String; Trust: Boolean; var Code: Integer): Boolean;
var
  Params: String;
begin
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + ScriptPath +
    '" -Action ' + Action;
  if Payload <> '' then Params := Params + ' -Payload "' + Payload + '"';
  if Trust then Params := Params + ' -TrustCertificate';
  // Never show a previous run's error for this run.
  DeleteFile(ExpandConstant('{autopf}\Mac Precision Touchpad State\last-error.txt'));
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Params,
    '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

function FailureMessage: String;
var
  Detail: TArrayOfString;
  I: Integer;
begin
  Result := 'Driver operation failed. Logs: ' + ExpandConstant('{autopf}\Mac Precision Touchpad State');
  // The error file is UTF-8 with a BOM; this keeps localized messages intact.
  if LoadStringsFromFile(ExpandConstant('{autopf}\Mac Precision Touchpad State\last-error.txt'), Detail) then
    for I := GetArrayLength(Detail) - 1 downto 0 do
      Result := Detail[I] + #13#10 + Result;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
  Architecture: String;
begin
  if IsArm64 then Architecture := 'arm64' else Architecture := 'x64';
  Log('Selected native driver architecture: ' + Architecture);
  // An upgrade replaces the battery service binary, which is locked while the
  // service runs. net stop waits until it has stopped; it fails harmlessly
  // when the service is absent.
  Exec(ExpandConstant('{sys}\net.exe'), 'stop AmtPtpBattery', '', SW_HIDE, ewWaitUntilTerminated, Code);
  ExtractTemporaryFiles('{tmp}\*.ps1');
  ExtractTemporaryFiles('{tmp}\' + Architecture + '\*');
  if not RunDriverTool('Validate', ExpandConstant('{tmp}\Manage-Driver.ps1'),
      ExpandConstant('{tmp}\' + Architecture), WizardIsTaskSelected('trustcert'), Code) then
    Result := 'Unable to start Windows PowerShell.'
  else if Code <> 0 then Result := FailureMessage
  else Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  if CurStep = ssPostInstall then begin
    if not RunDriverTool('Install', ExpandConstant('{app}\Manage-Driver.ps1'),
        ExpandConstant('{app}\driver'), WizardIsTaskSelected('trustcert'), Code) then
      RaiseException('Unable to start driver installation.');
    if (Code <> 0) and (Code <> 3010) then RaiseException(FailureMessage);
    RebootRequired := Code = 3010;
  end;
end;

function NeedRestart: Boolean;
begin
  Result := RebootRequired;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
begin
  // Runs after the user confirms and before any file is removed. Raising here
  // aborts the uninstall with every file and registration left in place.
  if CurUninstallStep = usUninstall then begin
    if not RunDriverTool('Uninstall', ExpandConstant('{app}\Manage-Driver.ps1'), '', False, Code) then
      RaiseException('Unable to start driver removal.');
    if (Code <> 0) and (Code <> 3010) then RaiseException(FailureMessage);
    RebootRequired := Code = 3010;
  end;
end;

function UninstallNeedRestart: Boolean;
begin
  Result := RebootRequired;
end;
