; ===========================================================================
;  Inno Setup script - TightVNC Server (com protecao de tela - Screen Guard)
;
;  Empacota os binarios compilados do TightVNC em um instalador unico.
;
;  Pre-requisitos:
;    1) Compilar a solucao com build-tvnserver.bat (ou manualmente):
;       - x64:  tightvnc\x64\Release\tvnserver.exe
;       - x86:  tightvnc\Release\tvnserver.exe
;    2) Inno Setup 6+ instalado (ISCC.exe).
;
;  Uso:
;    ISCC.exe tightvnc-setup.iss
;
;  O instalador gerado:
;    - Instala tvnserver.exe, screenhooks*.dll, hookldr.exe e sas.dll.
;    - Registra e inicia o servico "tvnserver" (modo servico).
;    - Cria atalhos no Menu Iniciar (Servidor, Controle, Config offline).
;    - Remove o servico na desinstalacao.
; ===========================================================================

#define MyAppName      "TightVNC Server"
#define MyAppVersion   "2.8.81"
#define MyAppPublisher "GlavSoft LLC."
#define MyAppExeName   "tvnserver.exe"

; Pasta raiz do codigo fonte (contendo os binarios compilados).
; Ajuste se necessario.
#ifndef SrcRoot
  #define SrcRoot "."
#endif

[Setup]
AppId={{1A2B3C4D-5E6F-4A5B-9C8D-0E1F2A3B4C5D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\TightVNC
DefaultGroupName=TightVNC
DisableProgramGroupPage=yes
OutputDir=Output
OutputBaseFilename=tightvnc-{#MyAppVersion}-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\tvnserver.exe
WizardStyle=modern
LicenseFile={#SrcRoot}\wix-installer\LICENSE.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Binarios 64 bits.
; NOTA: o MSBuild gera em Release\x64\ e Release\x86\.
Source: "{#SrcRoot}\Release\x64\tvnserver.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcRoot}\Release\x64\tvnviewer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcRoot}\Release\x64\screenhooks64.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcRoot}\Release\x86\screenhooks32.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcRoot}\Release\x64\hookldr.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcRoot}\wix-installer\files64\sas.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcRoot}\wix-installer\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
; Logo exibido no banner do screen guard.
Source: "{#SrcRoot}\logo.png"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\TightVNC Server - Control Interface"; Filename: "{app}\tvnserver.exe"; Parameters: "-controlservice"
Name: "{group}\TightVNC Server - Offline Configuration"; Filename: "{app}\tvnserver.exe"; Parameters: "-configservice"
Name: "{group}\Uninstall TightVNC"; Filename: "{uninstallexe}"

[Run]
; Registra o servico e o inicia silenciosamente.
Filename: "{app}\tvnserver.exe"; Parameters: "-reinstall -silent"; StatusMsg: "Registering TightVNC service..."; Flags: runhidden
Filename: "{app}\tvnserver.exe"; Parameters: "-start -silent"; StatusMsg: "Starting TightVNC service..."; Flags: runhidden
; Abre o TightVNC no Firewall do Windows (por programa, vale para qualquer porta).
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""TightVNC Server"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""TightVNC Server"" dir=in action=allow program=""{app}\tvnserver.exe"" enable=yes"; StatusMsg: "Opening Windows Firewall for TightVNC Server..."; Flags: runhidden
; Regra extra por porta (5900) para os casos em que o tvnserver roda como
; servico antes de o executavel estar no disco (cobre tambem o screen guard).
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""TightVNC Server (TCP 5900)"" dir=in action=allow protocol=TCP localport=5900"; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""TightVNC Server (TCP 5800)"" dir=in action=allow protocol=TCP localport=5800"; Flags: runhidden

[UninstallRun]
; Para e remove o servico antes de apagar os arquivos.
Filename: "{app}\tvnserver.exe"; Parameters: "-stop -silent"; Flags: runhidden
Filename: "{app}\tvnserver.exe"; Parameters: "-remove -silent"; Flags: runhidden
; Remove as regras de firewall.
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""TightVNC Server"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""TightVNC Server (TCP 5900)"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""TightVNC Server (TCP 5800)"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""TightVNC Server (RFB)"""; Flags: runhidden
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""TightVNC Server (HTTP)"""; Flags: runhidden

[Code]
// Nothing special needed: the screen guard is embedded in tvnserver.exe
// and is started automatically when a client connects.
