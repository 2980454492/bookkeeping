; Inno Setup 脚本 — 个人记账 Windows 安装包
; 编译前请先运行: PC\scripts\build-release.ps1
; 编译: "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" PC\packaging\bookkeeping.iss

#define MyAppName "个人记账"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Bookkeeping"
#define MyAppExeName "启动个人记账.bat"
#define PayloadDir "..\..\dist\Bookkeeping"

[Setup]
AppId={A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Bookkeeping
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=个人记账-{#MyAppVersion}-x64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\bookkeeping-server.exe

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加图标:"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; 切勿将开发机上的 data\*.db 打进安装包

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "立即启动 {#MyAppName}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 默认不删除用户数据；若需卸载时删除数据库，取消下一行注释：
; Type: filesandordirs; Name: "{app}\data"

[Code]
function InitializeSetup(): Boolean;
begin
  if not DirExists(ExpandConstant('{#PayloadDir}')) then
  begin
    MsgBox('未找到构建产物目录 dist\Bookkeeping\' + #13#10 +
      '请先运行: PC\scripts\build-release.ps1', mbError, MB_OK);
    Result := False;
  end
  else
    Result := True;
end;
