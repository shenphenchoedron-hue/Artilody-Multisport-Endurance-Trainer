; Inno Setup script for Artilody Watts Trainer (Windows x64).
;
; Built in CI (.github/workflows/release.yml):
;   iscc /DAppVersion=1.0.0 /DSourceDir=<staging dir> /DOutputDir=<dist dir> packaging\windows\installer.iss
;
; SourceDir must contain the windeployqt'ed application:
;   "Artilody Watts Trainer.exe", Qt DLLs, plugins, qml\, music\ ...

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\windows\Artilody Watts Trainer"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

#define AppName      "Artilody Watts Trainer"
#define AppExeName   "Artilody Watts Trainer.exe"
#define AppPublisher "Artilody"
#define AppURL       "https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer"

[Setup]
; Never change the AppId: it identifies the installation for upgrades/uninstall.
AppId={{38720AED-40E6-4B49-8382-73E74C446F4E}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
#if FileExists(AddBackslash(SourcePath) + "..\..\LICENSE")
LicenseFile=..\..\LICENSE
#endif
OutputDir={#OutputDir}
OutputBaseFilename=ArtilodyWattsTrainer-{#AppVersion}-windows-x64-setup
SetupIconFile=..\..\assets\icons\app-icon.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
MinVersion=10.0
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"
Name: "{autoprograms}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
