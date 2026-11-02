#ifndef SourceDir
  #error SourceDir must point to the staged portable directory
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
[Setup]
AppId={{32E19141-5E18-43E8-BCB2-793F563DE065}
AppName=Airlet PDF
AppVersion=0.2.0
AppPublisher=Airlet PDF
DefaultDirName={localappdata}\Programs\Airlet PDF
DefaultGroupName=Airlet PDF
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=AirletPDF-Setup-0.2.0-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\AirletPDF.exe
ChangesAssociations=yes
DisableProgramGroupPage=yes
InfoAfterFile=..\licenses\THIRD-PARTY.md
[Languages]
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
[Tasks]
Name: "desktopicon"; Description: "Vytvořit zástupce na ploše"; Flags: unchecked
Name: "pdfcandidate"; Description: "Nabídnout Airlet PDF jako aplikaci pro PDF (výchozí aplikaci zvolíte v nastavení Windows)"; Flags: unchecked
[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[InstallDelete]
; Remove only the unused network/touch files shipped by 0.1.0 on upgrades.
Type: files; Name: "{app}\generic\qtuiotouchplugin.dll"
Type: files; Name: "{app}\networkinformation\qnetworklistmanager.dll"
Type: files; Name: "{app}\tls\qcertonlybackend.dll"
Type: files; Name: "{app}\tls\qschannelbackend.dll"
Type: files; Name: "{app}\Qt6Network.dll"
[Icons]
Name: "{group}\Airlet PDF"; Filename: "{app}\AirletPDF.exe"
Name: "{autodesktop}\Airlet PDF"; Filename: "{app}\AirletPDF.exe"; Tasks: desktopicon
[Registry]
Root: HKCU; Subkey: "Software\Classes\Applications\AirletPDF.exe"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "Airlet PDF"; Tasks: pdfcandidate; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Applications\AirletPDF.exe\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""; Tasks: pdfcandidate
Root: HKCU; Subkey: "Software\Classes\Applications\AirletPDF.exe\shell\open\command"; ValueType: string; ValueData: """{app}\AirletPDF.exe"" ""%1"""; Tasks: pdfcandidate
Root: HKCU; Subkey: "Software\Classes\AirletPDF.Document"; ValueType: string; ValueData: "PDF dokument"; Tasks: pdfcandidate; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\AirletPDF.Document\DefaultIcon"; ValueType: string; ValueData: "{app}\AirletPDF.exe,0"; Tasks: pdfcandidate
Root: HKCU; Subkey: "Software\Classes\AirletPDF.Document\shell\open\command"; ValueType: string; ValueData: """{app}\AirletPDF.exe"" ""%1"""; Tasks: pdfcandidate
Root: HKCU; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: none; ValueName: "AirletPDF.Document"; Tasks: pdfcandidate; Flags: uninsdeletevalue
[Run]
Filename: "{app}\AirletPDF.exe"; Description: "Spustit Airlet PDF"; Flags: nowait postinstall skipifsilent
