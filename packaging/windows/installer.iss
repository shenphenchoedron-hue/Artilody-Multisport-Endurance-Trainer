; Inno Setup script for Artilody Multisport Endurance Trainer (Windows x64).
;
; Built in CI (.github/workflows/release.yml):
;   iscc /DAppVersion=1.5.0 /DSourceDir=<staging dir> /DOutputDir=<dist dir> packaging\windows\installer.iss
;
; SourceDir must contain the windeployqt'ed application:
;   "Artilody Multisport Endurance Trainer.exe", Qt DLLs, plugins, qml\, music\ ...

#ifndef AppVersion
  #define AppVersion "1.5.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\windows\Artilody Multisport Endurance Trainer"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

#define AppName      "Artilody Multisport Endurance Trainer"
#define AppExeName   "Artilody Multisport Endurance Trainer.exe"
#define AppPublisher "Artilody"
#define AppURL       "https://github.com/shenphenchoedron-hue/Artilody-Multisport-Endurance-Trainer"

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
OutputBaseFilename=ArtilodyMultisportEnduranceTrainer-{#AppVersion}-windows-x64-setup
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

; Upgrades: remove the deployed Qt runtime of the previous version first, so a
; module, plugin or DLL that the new version no longer ships (or ships under
; another name) is not left behind and loaded by mistake. Everything removed
; here is installed again from [Files]; user data lives in %APPDATA%, not {app}.
[InstallDelete]
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\plugins"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\iconengines"
Type: filesandordirs; Name: "{app}\multimedia"
Type: filesandordirs; Name: "{app}\sqldrivers"
Type: filesandordirs; Name: "{app}\tls"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\position"
Type: filesandordirs; Name: "{app}\qmltooling"
Type: files; Name: "{app}\Qt6*.dll"
Type: files; Name: "{app}\av*.dll"
Type: files; Name: "{app}\sw*.dll"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"
Name: "{autoprograms}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; IconFilename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
