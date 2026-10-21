; Installer for players: the same files as the zip (out\stage\SotC-PC, packaging/windows/package_sotc.sh),
; installed for the current user (no administrator rights), with shortcuts and an uninstaller.
; Built by build.cmd -Package: ISCC /DAppVersion=<version> /DSourceDir=<stage folder> /O<dist>.
; Saves, settings and caches (user\, sotc.ini, out\) are made by the game later and stay on uninstall.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\out\stage\SotC-PC"
#endif

[Setup]
AppId={{6E1E0C5B-2C0A-4E4B-9C55-5A0C0C0F5D11}
AppName=Shadow of the Colossus PC
AppVersion={#AppVersion}
AppVerName=Shadow of the Colossus PC {#AppVersion}
AppPublisher=Madmer
AppPublisherURL=https://github.com/MMadmer/ShadowOfTheColossus-PC
AppSupportURL=https://github.com/MMadmer/ShadowOfTheColossus-PC/issues
AppUpdatesURL=https://github.com/MMadmer/ShadowOfTheColossus-PC/releases
DefaultDirName={localappdata}\Programs\SotC-PC
; The folder is always asked (an update over an earlier install offers that folder): with the
; default "auto" the page was skipped once the game had been installed, and every update went to
; the first folder without a choice.
DisableDirPage=no
DefaultGroupName=Shadow of the Colossus PC
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile={#SourceDir}\launcher\sotc.ico
UninstallDisplayIcon={app}\SotC.exe
OutputBaseFilename=SotC-PC-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; Excludes: "sotc.ini"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; The launcher writes the player's keys into sotc.ini: an update keeps it.
Source: "{#SourceDir}\sotc.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{group}\Shadow of the Colossus PC"; Filename: "{app}\SotC.exe"
Name: "{group}\{cm:UninstallProgram,Shadow of the Colossus PC}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Shadow of the Colossus PC"; Filename: "{app}\SotC.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\SotC.exe"; Description: "{cm:LaunchProgram,Shadow of the Colossus PC}"; Flags: nowait postinstall skipifsilent
