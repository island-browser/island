; Inno Setup 6.3+ script for the per-user Windows installer. scripts/installers.py compiles it
; with ISCC and passes every /D define below; the source directory is the extracted release .zip,
; so the installer carries exactly the files the archive does.
;
;   AppVersion          Island version (SemVer)
;   SourceDir           extracted archive root (holds island_browser.exe)
;   OutputDir           where the setup program is written
;   OutputBaseFilename  setup file name without .exe
;   Arch                x64compatible or arm64

#ifndef AppVersion
  #error AppVersion must be defined
#endif

[Setup]
AppId={{931346E1-D512-4B8B-A48E-812B33C4D63F}
AppName=Island
AppVersion={#AppVersion}
AppVerName=Island {#AppVersion}
AppPublisher=Island
AppPublisherURL=https://island-browser.github.io/site/
AppSupportURL=https://github.com/island-browser/island/issues
; Per-user by default: no administrator prompt, and the in-browser updater can replace the
; install in place. The dialog still offers an all-users install.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\Island
DisableProgramGroupPage=yes
DisableDirPage=auto
ArchitecturesAllowed={#Arch}
ArchitecturesInstallIn64BitMode={#Arch}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
UninstallDisplayIcon={app}\island_browser.exe
UninstallDisplayName=Island

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; A previous version's files that this one no longer ships.
Type: filesandordirs; Name: "{app}\locales"

[Icons]
Name: "{autoprograms}\Island"; Filename: "{app}\island_browser.exe"
Name: "{autodesktop}\Island"; Filename: "{app}\island_browser.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\island_browser.exe"; Description: "{cm:LaunchProgram,Island}"; Flags: nowait postinstall skipifsilent
