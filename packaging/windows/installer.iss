; Inno Setup script for Artilody Soundsculp
; Compile with:  iscc /DAppVersion=1.0.0 /DSourceDir=<staging dir> installer.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\stage"
#endif

[Setup]
AppId={{7CFDA83C-940D-486B-8D3F-83AF890A90F8}
AppName=Artilody Soundsculp
AppVersion={#AppVersion}
AppPublisher=Artilody
AppPublisherURL=https://github.com/shenphenchoedron-hue/Artilody-Soundsculp
DefaultDirName={autopf}\Artilody Soundsculp
DefaultGroupName=Artilody Soundsculp
UninstallDisplayIcon={app}\Artilody Soundsculp.exe
OutputBaseFilename=ArtilodySoundsculp-{#AppVersion}-Windows-Setup
OutputDir=..\..\dist
SetupIconFile=icon.ico
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
LicenseFile=..\..\LICENSE
ChangesAssociations=yes

[Languages]
Name: "danish"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Artilody Soundsculp"; Filename: "{app}\Artilody Soundsculp.exe"
Name: "{group}\Manual (Dansk)"; Filename: "{app}\docs\Artilody-Soundsculp-Manual-DA.pdf"
Name: "{group}\Manual (English)"; Filename: "{app}\docs\Artilody-Soundsculp-Manual-EN.pdf"
Name: "{group}\{cm:UninstallProgram,Artilody Soundsculp}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Artilody Soundsculp"; Filename: "{app}\Artilody Soundsculp.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Artilody Soundsculp.exe"; Description: "{cm:LaunchProgram,Artilody Soundsculp}"; Flags: nowait postinstall skipifsilent
