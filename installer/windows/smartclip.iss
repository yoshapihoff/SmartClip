; ─────────────────────────────────────────────────────────────────────────
; SmartClip — установщик для Windows (Inno Setup 6)
;
; Собирается скриптом scripts/build-installer-windows.sh, который сам
; подставляет Define-переменные ниже. Вручную запускать так:
;
;   iscc.exe /DAppVersion=1.1.4 /DSourceDir=C:\stage\SmartClip \
;            /DOutputDir=C:\dist installer\windows\smartclip.iss
;
; Ставится в Program Files, создаёт ярлыки в меню «Пуск» и (опционально)
; в автозапуске. Удаление — через «Программы и компоненты».
; ─────────────────────────────────────────────────────────────────────────

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build-windows\win-stage\SmartClip"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif
; Корень проекта (LICENSE, icons/). Скрипт передаёт его явно, иначе
; выводим из SourceDir (stage лежит в <project>/build-windows/win-stage/SmartClip).
#ifndef ProjectDir
  #define ProjectDir SourceDir + "\..\.."
#endif

#define AppName        "SmartClip"
#define AppPublisher   "Aleksey Zhmikhov"
#define AppURL         "https://git.halfpi.ru/mario/smartclip"
#define AppExeName     "SmartClip.exe"

[Setup]
AppId={{8C4B2E31-7A5D-4F62-9B1E-4D3C8A90F721}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#ProjectDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-windows-x86_64-setup
SetupIconFile={#ProjectDir}\icons\SmartClip.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; x64-приложение: не даём поставить в 32-битную систему
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Ставим в Program Files — нужны права администратора
PrivilegesRequired=admin
UninstallDisplayIcon={app}\{#AppExeName}
MinVersion=10.0

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart";   Description: "Запускать SmartClip при входе в систему"; GroupDescription: "Автозапуск:"

[Files]
; Весь портативный набор: .exe, Qt-DLL, плагины (platforms/, styles/, tls/), OpenSSL
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}";           Filename: "{app}\{#AppExeName}"
Name: "{group}\Удалить {#AppName}";   Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";     Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Registry]
; Автозапуск — та же ветка реестра, что использует само приложение
; (LaunchAgentManager), чтобы состояние совпадало.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "SmartClip"; \
    ValueData: """{app}\{#AppExeName}"""; \
    Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; Закрыть приложение перед удалением, иначе файлы заняты.
Filename: "{sys}\taskkill.exe"; Parameters: "/F /IM {#AppExeName}"; \
    Flags: runhidden skipifdoesntexist; RunOnceId: "KillApp"

[Code]
// Если приложение запущено — предложить закрыть перед установкой.
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  Result := True;
  if Exec('tasklist.exe', '/FI "IMAGENAME eq {#AppExeName}"', '', SW_HIDE,
          ewWaitUntilTerminated, ResultCode) then
  begin
    // Ничего не делаем: taskkill в [UninstallRun] и перезапись файлов
    // Inno отработает сама (в крайнем случае попросит перезагрузку).
  end;
end;
