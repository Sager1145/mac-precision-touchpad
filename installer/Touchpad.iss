#ifndef PackageRoot
  #error PackageRoot must point to the prepared release directory
#endif
#ifndef AppVersion
  #define AppVersion "0.2.0-preview.2"
#endif
[Setup]
AppId={{B9DA7434-8AF2-4F63-84D8-179566B48BD5}
AppName=Mac Precision Touchpad (Sager1145)
AppVersion={#AppVersion}
VersionInfoVersion=0.2.0.0
AppPublisher=Sager1145
AppPublisherURL=https://github.com/Sager1145/mac-precision-touchpad
AppSupportURL=https://github.com/Sager1145/mac-precision-touchpad/issues
AppUpdatesURL=https://github.com/Sager1145/mac-precision-touchpad/releases
DefaultDirName={autopf}\Mac Precision Touchpad
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

[Tasks]
Name: trustcert; Description: "Trust the bundled public WDK test certificate for driver installation"; Flags: unchecked

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
    '" -Action ' + Action + ' -StateDirectory "' + ExpandConstant('{commonappdata}\Sager1145\MacPrecisionTouchpad') + '"';
  if Payload <> '' then Params := Params + ' -Payload "' + Payload + '"';
  if Trust then Params := Params + ' -TrustCertificate';
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Params,
    '', SW_HIDE, ewWaitUntilTerminated, Code);
end;

function FailureMessage: String;
var
  Detail: AnsiString;
begin
  Result := 'Driver operation failed. Logs: ' + ExpandConstant('{commonappdata}\Sager1145\MacPrecisionTouchpad');
  if LoadStringFromFile(ExpandConstant('{commonappdata}\Sager1145\MacPrecisionTouchpad\last-error.txt'), Detail) then
    Result := String(Detail) + #13#10 + Result;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
  Architecture: String;
begin
  if IsArm64 then Architecture := 'arm64' else Architecture := 'x64';
  Log('Selected native driver architecture: ' + Architecture);
  ExtractTemporaryFiles('{tmp}\*.ps1');
  ExtractTemporaryFiles('{tmp}\' + Architecture + '\*');
  if not RunDriverTool('Validate', ExpandConstant('{tmp}\Manage-Driver.ps1'),
      ExpandConstant('{tmp}\' + Architecture), False, Code) then
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

function InitializeUninstall: Boolean;
var
  Code: Integer;
begin
  Result := False;
  if not UninstallSilent then
    if MsgBox('Remove this installer''s driver packages? The trackpad may disconnect briefly. Keep another input device available. Backups and logs will be retained.',
        mbConfirmation, MB_YESNO or MB_DEFBUTTON2) <> IDYES then Exit;
  if not RunDriverTool('Uninstall', ExpandConstant('{app}\Manage-Driver.ps1'), '', False, Code) then begin
    MsgBox('Unable to start driver removal.', mbError, MB_OK); Exit;
  end;
  if (Code <> 0) and (Code <> 3010) then begin
    MsgBox(FailureMessage, mbError, MB_OK); Exit;
  end;
  RebootRequired := Code = 3010;
  Result := True;
end;

function UninstallNeedRestart: Boolean;
begin
  Result := RebootRequired;
end;
