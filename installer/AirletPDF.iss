#ifndef SourceDir
  #error SourceDir must point to the staged portable directory
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
[Setup]
AppId={{32E19141-5E18-43E8-BCB2-793F563DE065}
AppName=Airlet PDF
AppVersion=0.6.5
AppPublisher=Trimenor
AppPublisherURL=https://github.com/zafodtom/airlet-pdf
AppSupportURL=https://github.com/zafodtom/airlet-pdf/issues
AppUpdatesURL=https://github.com/zafodtom/airlet-pdf/releases/latest
DefaultDirName={localappdata}\Programs\Airlet PDF
DefaultGroupName=Airlet PDF
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=AirletPDF-Setup-0.6.5-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\assets\airlet.ico
UninstallDisplayIcon={app}\AirletPDF.exe
ChangesAssociations=yes
DisableProgramGroupPage=yes
InfoAfterFile=..\licenses\THIRD-PARTY.md
[Languages]
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
[CustomMessages]
czech.DesktopIcon=Vytvořit zástupce na ploše
english.DesktopIcon=Create a desktop shortcut
czech.LaunchApp=Spustit Airlet PDF
english.LaunchApp=Launch Airlet PDF
czech.DefaultPDF=Zvolit Airlet PDF jako výchozí pro PDF v nastavení Windows
english.DefaultPDF=Choose Airlet PDF as the default PDF app in Windows Settings
czech.PDFDocument=PDF dokument
english.PDFDocument=PDF document
czech.AppDescription=Rychlý lokální prohlížeč PDF
english.AppDescription=A fast local PDF viewer
[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; Flags: unchecked
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[InstallDelete]
; Remove only the unused network/touch files shipped by 0.1.0 on upgrades.
Type: files; Name: "{app}\generic\qtuiotouchplugin.dll"
[Icons]
Name: "{group}\Airlet PDF"; Filename: "{app}\AirletPDF.exe"
Name: "{autodesktop}\Airlet PDF"; Filename: "{app}\AirletPDF.exe"; Tasks: desktopicon
[Registry]
Root: HKCU; Subkey: "Software\Classes\Applications\AirletPDF.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Airlet PDF"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\AirletPDF.exe\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\Applications\AirletPDF.exe\shell\open\command"; ValueType: string; ValueData: """{app}\AirletPDF.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\AirletPDF.Document"; ValueType: string; ValueData: "{cm:PDFDocument}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\AirletPDF.Document\DefaultIcon"; ValueType: string; ValueData: "{app}\AirletPDF.exe,0"
Root: HKCU; Subkey: "Software\Classes\AirletPDF.Document\shell\open\command"; ValueType: string; ValueData: """{app}\AirletPDF.exe"" ""%1"""
Root: HKCU; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "AirletPDF.Document"; ValueData: ""; Flags: uninsdeletevalue
Root: HKCU; Subkey: "Software\Airlet\Airlet PDF\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "Airlet PDF"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Airlet\Airlet PDF\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "{cm:AppDescription}"
Root: HKCU; Subkey: "Software\Airlet\Airlet PDF\Capabilities"; ValueType: string; ValueName: "ApplicationIcon"; ValueData: "{app}\AirletPDF.exe,0"
Root: HKCU; Subkey: "Software\Airlet\Airlet PDF\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "AirletPDF.Document"
Root: HKCU; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "Airlet PDF"; ValueData: "Software\Airlet\Airlet PDF\Capabilities"; Flags: uninsdeletevalue
[Run]
Filename: "{app}\AirletPDF.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent
Filename: "{code:DefaultAppsUri}"; Description: "{cm:DefaultPDF}"; Flags: postinstall shellexec skipifsilent

[Code]
function DefaultAppsUri(Param: String): String;
var
  Version: TWindowsVersion;
begin
  GetWindowsVersionEx(Version);
  if Version.Build >= 22000 then
    Result := 'ms-settings:defaultapps?registeredAppUser=Airlet%20PDF'
  else
    Result := 'ms-settings:defaultapps';
end;
