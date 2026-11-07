; Part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler.
; Copyright (C) 2026 Daniel Jameson. GPL v3 or later.
;
; The Windows installer (Inno Setup 6.3 or later), made from deploy/ as core/ci/build.sh windows leaves it, with LICENSE,
; THIRD-PARTY-NOTICES.md and licences/ copied in (the workflow's Package step). The workflow runs it for a release
; tag (and for Run workflow), from the program's source root, where the core is the submodule core/:
;
;   iscc /DProgName=acorndiskwrangler "/DProgTitle=Acorn Disk Wrangler" "/DProgFolder=Acorn Disk Wrangler"
;        /DProgVersion=1.0.0 /DProgNumVersion=1.0.0 "/DProgCopyright=Copyright (C) 2026 Daniel Jameson"
;        /DProgUrl=https://github.com/drdpj/AcornDiskWrangler /DDeployDir=C:\...\deploy /DOutDir=C:\...\dist
;        /DOutName=acorndiskwrangler-v1.0.0-windows-x64-setup core\ci\installer.iss
;
; SourceRoot (default: two folders up from this file, which is the program's source root when the core is its core/
; submodule) is where LICENSE, icons\ and build\installer-delete.iss are looked for.
;
; ProgFolder is ProgTitle made fit for a folder or shortcut name ("CP/M" -> "CPM"). It installs for all users by
; default, into Program Files (Windows asks for an administrator), and its first page offers "only for me" instead
; (no administrator, into %LOCALAPPDATA%\Programs; also /CURRENTUSER on the command line, as the workflow's check
; uses). An install over an older version replaces it. Each program keeps its own AppId below, for good: Windows
; knows an installed program by it.
;
; Before anything else, setup looks for an installed copy (the AppId's uninstall entry, for all users and for the
; current user) and says what it found: an upgrade, a downgrade (No is the default button), the same version again,
; or a copy installed the other way (all users / only for me) that this one will sit beside. The user can go on or
; stop. /SUPPRESSMSGBOXES answers Yes, so silent installs still upgrade and downgrade; the log has an
; "Installed copy:" line for each copy found.
;
; Qt's DLLs and plugin folders change between releases (MSYS2's Qt moves on), so an update first deletes the DLLs
; beside the program and each folder this version ships; the workflow lists those folders in
; build\installer-delete.iss. Anything else in the program's folder is kept. An installed program has no portable
; mode (appsettings.h: it sees Inno Setup's unins000.dat beside it), so File > Create local config file isn't shown.

#ifndef ProgName
  #error ProgName (the program, e.g. acorndiskwrangler) must be given: /DProgName=...
#endif

#if ProgName == "acorndiskwrangler"
  #define AppGuidRaw "{88BAD00B-21F2-483B-8186-447786EAA280}"
#elif ProgName == "cpmdiskwrangler"
  #define AppGuidRaw "{4EF59823-E240-48C7-9454-6D5F7314CA66}"
#else
  #error Unknown program: add its own AppId (a new GUID) here
#endif
; [Setup] needs the opening brace doubled; the uninstall key in the registry is the plain GUID + "_is1"
#define AppGuid "{" + AppGuidRaw

#ifndef ProgTitle
  #define ProgTitle ProgName
#endif
#ifndef ProgFolder
  #define ProgFolder ProgTitle
#endif
#ifndef ProgVersion
  #define ProgVersion "0.0.0"
#endif
#ifndef ProgNumVersion
  #define ProgNumVersion "0.0.0"
#endif
#ifndef ProgCopyright
  #define ProgCopyright "Copyright (C) 2026 Daniel Jameson"
#endif
#ifndef ProgUrl
  #define ProgUrl "https://github.com/drdpj"
#endif
#ifndef SourceRoot
  #define SourceRoot AddBackslash(SourcePath) + "..\.."
#endif
#ifndef DeployDir
  #define DeployDir AddBackslash(SourceRoot) + "deploy"
#endif
#ifndef OutDir
  #define OutDir AddBackslash(SourceRoot) + "dist"
#endif
#ifndef OutName
  #define OutName ProgName + "-" + ProgVersion + "-windows-x64-setup"
#endif

[Setup]
AppId={#AppGuid}
AppName={#ProgTitle}
AppVersion={#ProgVersion}
AppVerName={#ProgTitle} {#ProgVersion}
AppPublisher=Daniel Jameson
AppPublisherURL={#ProgUrl}
AppSupportURL={#ProgUrl}/issues
AppUpdatesURL={#ProgUrl}/releases
AppCopyright={#ProgCopyright}
VersionInfoVersion={#ProgNumVersion}
VersionInfoProductName={#ProgTitle}
VersionInfoDescription={#ProgTitle} setup
DefaultDirName={autopf}\{#ProgFolder}
DefaultGroupName={#ProgFolder}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#AddBackslash(SourceRoot)}LICENSE
SetupIconFile={#AddBackslash(SourceRoot)}icons\{#ProgName}.ico
UninstallDisplayIcon={app}\{#ProgName}.exe
UninstallDisplayName={#ProgTitle}
OutputDir={#OutDir}
OutputBaseFilename={#OutName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

#if FileExists(AddBackslash(SourceRoot) + "build\installer-delete.iss")
  #include AddBackslash(SourceRoot) + "build\installer-delete.iss"
#endif

[Files]
Source: "{#DeployDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#ProgFolder}"; Filename: "{app}\{#ProgName}.exe"
Name: "{autodesktop}\{#ProgFolder}"; Filename: "{app}\{#ProgName}.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ProgName}.exe"; Description: "{cm:LaunchProgram,{#ProgFolder}}"; Flags: nowait postinstall skipifsilent

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#AppGuidRaw}_is1';

// The next number of a version such as "1.2.3", "v1.2.0-beta.1" or "1.1.0-15-gabc1234", taken off the front of S
function NextNum(var S: String): Integer;
var
  I: Integer;
begin
  Result := 0;
  I := 1;
  while (I <= Length(S)) and (S[I] >= '0') and (S[I] <= '9') do begin
    Result := Result * 10 + Ord(S[I]) - Ord('0');
    I := I + 1;
  end;
  if (I <= Length(S)) and (S[I] = '.') then
    I := I + 1;
  Delete(S, 1, I - 1);
end;

// -1, 0 or 1 as A's major.minor.patch is below, equal to or above B's (anything after them is not compared)
function CompareVersions(A, B: String): Integer;
var
  I, X, Y: Integer;
begin
  Result := 0;
  if (Length(A) > 0) and ((A[1] = 'v') or (A[1] = 'V')) then Delete(A, 1, 1);
  if (Length(B) > 0) and ((B[1] = 'v') or (B[1] = 'V')) then Delete(B, 1, 1);
  for I := 1 to 3 do begin
    X := NextNum(A);
    Y := NextNum(B);
    if X < Y then begin Result := -1; Exit; end;
    if X > Y then begin Result := 1; Exit; end;
  end;
end;

// The installed copy's version and folder from its uninstall entry under Root; False if there is none
function FindInstalled(Root: Integer; var Ver, Dir: String): Boolean;
begin
  Ver := '';
  Dir := '';
  Result := RegQueryStringValue(Root, UninstallKey, 'DisplayVersion', Ver);
  if Result then begin
    RegQueryStringValue(Root, UninstallKey, 'InstallLocation', Dir);
    Dir := RemoveBackslashUnlessRoot(Dir);
    if Ver = '' then Ver := '(unknown version)';
  end;
end;

function InitializeSetup(): Boolean;
var
  ThisRoot, OtherRoot, Buttons, Cmp: Integer;
  Ver, Dir, Where, Msg, ThisScope, OtherScope: String;
  Found: Boolean;
begin
  Result := True;
  Msg := '';
  Buttons := MB_YESNO;
  if IsAdminInstallMode then begin
    ThisRoot := HKLM64; OtherRoot := HKCU;
    ThisScope := 'for all users'; OtherScope := 'for the current user only';
  end else begin
    ThisRoot := HKCU; OtherRoot := HKLM64;
    ThisScope := 'for the current user only'; OtherScope := 'for all users';
  end;

  Found := FindInstalled(ThisRoot, Ver, Dir);
  if Found then begin
    Log('Installed copy: ' + Ver + ' ' + ThisScope + ' in ' + Dir);
    Where := '';
    if Dir <> '' then Where := ' in ' + Dir;
    Cmp := CompareVersions(Ver, '{#ProgVersion}');
    if Cmp < 0 then
      Msg := '{#ProgTitle} ' + Ver + ' is installed' + Where + '.' + #13#10#13#10 +
             'Setup will upgrade it to version {#ProgVersion}.'
    else if Cmp > 0 then begin
      Msg := 'A newer version of {#ProgTitle}, ' + Ver + ', is installed' + Where + '.' + #13#10#13#10 +
             'Setup will replace it with the older version {#ProgVersion}. Your settings are kept.';
      Buttons := MB_YESNO or MB_DEFBUTTON2;
    end else if CompareText(Ver, '{#ProgVersion}') = 0 then
      Msg := '{#ProgTitle} {#ProgVersion} is already installed' + Where + '.' + #13#10#13#10 +
             'Setup will install it again over the same files.'
    else
      Msg := '{#ProgTitle} ' + Ver + ' is installed' + Where + '.' + #13#10#13#10 +
             'Setup will replace it with {#ProgVersion}, another build of the same version.';
  end;

  if FindInstalled(OtherRoot, Ver, Dir) then begin
    Log('Installed copy: ' + Ver + ' ' + OtherScope + ' in ' + Dir);
    if Msg <> '' then Msg := Msg + #13#10#13#10;
    Msg := Msg + '{#ProgTitle} ' + Ver + ' is also installed ' + OtherScope;
    if Dir <> '' then Msg := Msg + ', in ' + Dir;
    Msg := Msg + '. This installation (' + ThisScope + ') will be a separate copy beside it; ' +
           'remove the other one in Settings > Apps if you only want one.';
  end;

  if Msg <> '' then
    Result := SuppressibleMsgBox(Msg + #13#10#13#10 + 'Continue?', mbConfirmation, Buttons, IDYES) = IDYES;
end;
