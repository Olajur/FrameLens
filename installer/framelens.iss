; FrameLens — Windows installer (Inno Setup 6).
;
; Build:  tools\build-installer.bat   (builds the app, then this script)
; Output: build\installer\FrameLens-<version>-setup.exe
;
; What it does:
;   - lets the user choose the install folder (default: Program Files\FrameLens)
;   - installs framelens.exe, framelens-cli.exe, the ETW-access helper and the licenses
;   - Start-menu shortcut; optional desktop shortcut and "start when I sign in"
;   - optional (on by default): allows FPS measurement by adding the user to "Performance Log
;     Users" (tools\setup-etw-access.ps1, run as the signed-in user, not the admin account)
;   - registers an uninstaller (Settings > Apps); uninstall can also undo the group membership
;     and delete the user's FrameLens settings
; SPDX-License-Identifier: MIT

#define AppName "FrameLens"
; AppVersion: keep in step with project(... VERSION) in CMakeLists.txt and FILEVERSION in src\app\framelens.rc
#define AppVersion "0.3.2"
#define AppExe "framelens.exe"
#define Root ".."

[Setup]
AppId={{6F3C2B1E-8A4D-4E5F-9B7C-2D1A0E9F4C3B}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
AppComments=FPS and hardware monitor with an on-screen overlay
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
; Admin: needed for Program Files and for the "Performance Log Users" group.
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#Root}\build\installer
OutputBaseFilename=FrameLens-{#AppVersion}-setup
SetupIconFile={#Root}\src\app\framelens.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; FrameLens must not be running while it is replaced or removed (single-instance mutex in the app).
AppMutex=Local\FrameLens.App
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "etwaccess"; Description: "Allow FPS measurement (adds you to the Windows ""Performance Log Users"" group; Windows may ask for permission once)"; GroupDescription: "FPS measurement:"
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "autostart"; Description: "Start FrameLens when I sign in to Windows"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#Root}\build\framelens.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\build\framelens-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\tools\setup-etw-access.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#Root}\tools\setup-etw-access.bat"; DestDir: "{app}\tools"; Flags: ignoreversion
Source: "{#Root}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Root}\LICENSE"; DestDir: "{app}\licenses"; DestName: "FrameLens-LICENSE.txt"; Flags: ignoreversion
Source: "{#Root}\third_party\presentmon\LICENSE.txt"; DestDir: "{app}\licenses"; DestName: "PresentData-LICENSE.txt"; Flags: ignoreversion
Source: "{#Root}\third_party\igcl\License.txt"; DestDir: "{app}\licenses"; DestName: "Intel-IGCL-LICENSE.txt"; Flags: ignoreversion
Source: "{#Root}\third_party\adl-notice\LICENSE.txt"; DestDir: "{app}\licenses"; DestName: "AMD-ADL-NOTICE.txt"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; "Start when I sign in": FrameLens writes its own per-user Run entry, as the signed-in user
; (an elevated setup would otherwise write it for the administrator account). Also in the tray menu.
Filename: "{app}\{#AppExe}"; Parameters: "--autostart on"; Tasks: autostart; Flags: runasoriginaluser runhidden waituntilterminated
; Started as the signed-in user, never elevated.
Filename: "{app}\{#AppExe}"; Description: "Start {#AppName} now"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--autostart off"; Flags: runhidden waituntilterminated; RunOnceId: "RemoveAutostart"

[Code]
const
  EtwAdded = 0;      // exit codes of tools\setup-etw-access.ps1
  EtwNoChange = 1;

var
  EtwJustAdded: Boolean;
  GpuPage: TOutputMsgMemoWizardPage;

// ---------------------------------------------------------------- graphics card detection
// FrameLens reads temperature, clocks, power and fan from the GPU vendor's own driver library
// (AMD: ADL, atiadlxx.dll / NVIDIA: NVML, nvml.dll / Intel: IGCL, ControlLib.dll). This page shows
// which cards Windows reports and whether that library is installed. The app itself detects the
// GPU again at every start, so a new card or driver update needs no reinstall.

function VendorOf(const Pnp: String; var Name, Library, Dll: String): Boolean;
var
  Ven: String;
begin
  Result := True;
  Ven := Copy(Pnp, Pos('VEN_', Pnp) + 4, 4);
  if Ven = '1002' then begin Name := 'AMD'; Library := 'AMD driver (ADL)'; Dll := 'atiadlxx.dll'; end
  else if Ven = '10DE' then begin Name := 'NVIDIA'; Library := 'NVIDIA driver (NVML)'; Dll := 'nvml.dll'; end
  else if Ven = '8086' then begin Name := 'Intel'; Library := 'Intel driver (IGCL)'; Dll := 'ControlLib.dll'; end
  else Result := False;
end;

function DescribeGpus(): String;
var
  Locator, Service, Items, Item: Variant;
  I, Found: Integer;
  Pnp, GpuName, Driver, VendorName, Library, Dll: String;
begin
  Result := '';
  Found := 0;
  try
    Locator := CreateOleObject('WbemScripting.SWbemLocator');
    Service := Locator.ConnectServer('.', 'root\CIMV2');
    Items := Service.ExecQuery('SELECT Name, PNPDeviceID, DriverVersion FROM Win32_VideoController');
    for I := 0 to Items.Count - 1 do begin
      Item := Items.ItemIndex(I);
      try
        Pnp := Item.PNPDeviceID;
        GpuName := Item.Name;
        Driver := Item.DriverVersion;
      except
        Pnp := '';   // entry with an empty (Null) field
      end;
      Pnp := Uppercase(Pnp);
      // Real graphics cards only: virtual displays (Virtual Desktop, Sunshine, Meta Link...) are ROOT\ devices.
      if Pos('PCI\', Pnp) <> 1 then Continue;
      Found := Found + 1;
      Result := Result + GpuName + '   (driver ' + Driver + ')' + #13#10;
      if not VendorOf(Pnp, VendorName, Library, Dll) then
        Result := Result + '    No sensor support for this vendor: FrameLens shows load and VRAM only.' + #13#10
      else if FileExists(ExpandConstant('{sys}\') + Dll) then
        Result := Result + '    Sensors: ' + Library + ' found. Temperature, clocks, power and fan will be shown.' + #13#10
      else
        Result := Result + '    Sensors: ' + Library + ' NOT found (' + Dll + ').' + #13#10 +
                  '    Install or update the ' + VendorName + ' graphics driver; until then FrameLens shows load and VRAM only.' + #13#10;
      Result := Result + #13#10;
    end;
  except
    Result := 'Windows did not report the graphics cards (' + GetExceptionMessage + ').' + #13#10 +
              'FrameLens detects the GPU itself when it starts, so this does not stop the installation.' + #13#10;
    Exit;
  end;
  if Found = 0 then
    Result := 'No graphics card was reported by Windows.' + #13#10 +
              'FrameLens detects the GPU itself when it starts; sensors need the AMD, NVIDIA or Intel graphics driver.' + #13#10
  else
    Result := Result + 'FrameLens detects the GPU again every time it starts, so a new card or driver needs no reinstall.';
end;

procedure InitializeWizard();
begin
  GpuPage := CreateOutputMsgMemoPage(wpWelcome, 'Graphics card', 'Sensors for the GPU in this PC',
    'FrameLens reads temperature, clocks, power and fan speed from your graphics driver:', DescribeGpus());
end;

function EtwScript(const Action: String): String;
begin
  Result := '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\tools\setup-etw-access.ps1') + '" -Action ' + Action;
end;

function PowerShellExe(): String;
begin
  Result := ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('etwaccess') then begin
    // Run as the signed-in (original) user, so that user is added even when an administrator's
    // credentials were typed into the UAC prompt. The script asks for its own approval if needed.
    if ExecAsOriginalUser(PowerShellExe(), EtwScript('add'), '', SW_HIDE, ewWaitUntilTerminated, Code) then begin
      EtwJustAdded := (Code = EtwAdded);
      if (Code <> EtwAdded) and (Code <> EtwNoChange) then
        MsgBox('FrameLens was installed, but FPS measurement could not be enabled (code ' + IntToStr(Code) + ').' + #13#10 + #13#10 +
               'You can try again later: run "tools\setup-etw-access.bat" in the FrameLens folder.', mbInformation, MB_OK);
    end;
  end;
end;

// A new group membership only applies after signing out / restarting.
function NeedRestart(): Boolean;
begin
  Result := EtwJustAdded;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
begin
  if CurUninstallStep = usUninstall then begin
    if MsgBox('Also remove your account from the "Performance Log Users" group?' + #13#10 + #13#10 +
              'FrameLens added it to allow FPS measurement. Choose No if other tools (for example PresentMon or CapFrameX) need it.',
              mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      Exec(PowerShellExe(), EtwScript('remove'), '', SW_HIDE, ewWaitUntilTerminated, Code);
  end;
  if CurUninstallStep = usPostUninstall then begin
    if DirExists(ExpandConstant('{localappdata}\FrameLens')) and
       (MsgBox('Delete your FrameLens settings (overlay layout, size, monitor) too?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
      DelTree(ExpandConstant('{localappdata}\FrameLens'), True, True, True);
  end;
end;
