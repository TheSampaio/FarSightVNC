; FarSightVNC viewer-only, per-user installer. Requires Inno Setup 6.3 or later.
; Build through Build-Installer.ps1; never point PayloadDir at the build tree.
#ifndef PayloadDir
  #error PayloadDir must name the validated flat shipping directory
#endif
#ifndef AppVersion
  #error AppVersion must come from fsvncviewer.exe
#endif
#ifndef NumericVersion
  #error NumericVersion must come from fsvncviewer.exe
#endif
#ifndef InstallerOutputDir
  #error InstallerOutputDir must be supplied
#endif

[Setup]
AppId=FarSightVNC.Viewer
AppName=FarSightVNC
AppVersion={#AppVersion}
AppPublisher=FarSightVNC contributors
AppPublisherURL=https://github.com/TheSampaio/FarSightVNC
AppSupportURL=https://github.com/TheSampaio/FarSightVNC/issues
AppUpdatesURL=https://github.com/TheSampaio/FarSightVNC/releases
DefaultDirName={localappdata}\Programs\FarSightVNC
DefaultGroupName=FarSightVNC
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.10240
WizardStyle=modern
LicenseFile={#PayloadDir}\LICENSE.txt
UninstallDisplayIcon={app}\fsvncviewer.exe
SetupIconFile=..\..\assets\icons\logo-far-sigh-vnc.ico
OutputDir={#InstallerOutputDir}
OutputBaseFilename=FarSightVNC-v{#AppVersion}-Setup-x64
VersionInfoVersion={#NumericVersion}
VersionInfoProductVersion={#AppVersion}
Compression=lzma2
SolidCompression=yes
CloseApplications=no
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\fsvncviewer.exe"; DestDir: "{app}"
Source: "{#PayloadDir}\fsvncupdater.exe"; DestDir: "{app}"
Source: "{#PayloadDir}\LEIAME.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\FarSightVNC"; Filename: "{app}\fsvncviewer.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\FarSightVNC"; Filename: "{app}\fsvncviewer.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\fsvncviewer.exe"; Description: "{cm:LaunchProgram,FarSightVNC}"; Flags: nowait postinstall skipifsilent unchecked
