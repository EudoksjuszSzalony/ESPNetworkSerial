#define MyAppName "ESPNetworkSerial"
#define MyAppPublisher "EudoksjuszSzalony"
#define MyAppURL "https://github.com/EudoksjuszSzalony/ESPNetworkSerial"
#define MyAppExeName "espnetworkserial-monitor.exe"

#ifndef AppVersion
  #define AppVersion "0.0.18-dev"
#endif

[Setup]
AppId={{9F0D8603-FF61-4FD7-8A95-93D3F94DE983}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppVerName={#MyAppName} {#AppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
DefaultDirName={localappdata}\Programs\ESPNetworkSerial
DefaultGroupName=ESPNetworkSerial
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
OutputDir=output
OutputBaseFilename=espnetworkserial-setup-{#AppVersion}-windows-amd64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile=payload\LICENSE
CloseApplications=no
RestartApplications=no
SetupLogging=yes

[Files]
Source: "payload\espnetworkserial-monitor.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\README.md"; DestDir: "{app}"; DestName: "MONITOR-README.md"; Flags: ignoreversion
Source: "payload\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\config.example.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "register-arduino.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "unregister-arduino.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion

[Icons]
Name: "{group}\Open ESPNetworkSerial folder"; Filename: "{sys}\explorer.exe"; Parameters: """{app}"""
Name: "{group}\Repair Arduino integration"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\register-arduino.ps1"" -MonitorPath ""{app}\{#MyAppExeName}"""
Name: "{group}\Uninstall ESPNetworkSerial"; Filename: "{uninstallexe}"

[UninstallRun]
Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\tools\unregister-arduino.ps1"""; Flags: runhidden waituntilterminated

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  PowerShell: String;
  ScriptPath: String;
  MonitorPath: String;
  Params: String;
begin
  if CurStep <> ssPostInstall then
    exit;

  PowerShell := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
  ScriptPath := ExpandConstant('{app}\tools\register-arduino.ps1');
  MonitorPath := ExpandConstant('{app}\{#MyAppExeName}');
  Params :=
    '-NoProfile -ExecutionPolicy Bypass -File "' + ScriptPath +
    '" -MonitorPath "' + MonitorPath + '"';

  if not Exec(PowerShell, Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    MsgBox(
      'ESPNetworkSerial was installed, but Arduino integration could not be started.'#13#10 +
      'Run "Repair Arduino integration" from the Start menu after setup.',
      mbInformation, MB_OK);
    exit;
  end;

  if ResultCode <> 0 then
  begin
    MsgBox(
      'ESPNetworkSerial was installed, but one or more Arduino core versions could not be configured.'#13#10 +
      'See integration-status.txt in the installation folder, then use "Repair Arduino integration" after resolving the conflict.',
      mbInformation, MB_OK);
  end;
end;
