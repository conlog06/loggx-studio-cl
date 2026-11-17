; Inno-Setup-Skript für LXStudio (Windows 10/11, 64 Bit).
; Aufruf: ISCC.exe /DAppVersion=0.1.0 /DSourceDir=<dist-Ordner> /DOutputDir=<Ziel> LoggXStudio.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\LoggX Studio"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

#define AppName "LXStudio"
#define AppExe "LoggX Studio.exe"

[Setup]
AppId={{7E0C4B5A-2F3D-4C8E-9B61-4A1D3C9E8F21}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=LoggX
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=LXStudio-{#AppVersion}-Windows-Setup
SetupIconFile=..\..\app\resources\loggx.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "assoc"; Description: "LoggX-Projekte (.loggx) mit {#AppName} öffnen"; GroupDescription: "Dateizuordnung:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; Verknüpfungen unter dem alten Namen (bis 0.6) entfernen
Type: files; Name: "{autoprograms}\LoggX Studio.lnk"
Type: files; Name: "{autodesktop}\LoggX Studio.lnk"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.loggx"; ValueType: string; ValueName: ""; ValueData: "LoggXStudio.Project"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LoggXStudio.Project"; ValueType: string; ValueName: ""; ValueData: "LoggX-Projekt"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LoggXStudio.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LoggXStudio.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
; Automatisches Update (still, mit /UPDATE): danach LXStudio wieder starten
Filename: "{app}\{#AppExe}"; Flags: nowait runasoriginaluser; Check: IsUpdate

[Code]
function IsUpdate: Boolean;
begin
  Result := Pos('/UPDATE', UpperCase(GetCmdTail)) > 0;
end;
