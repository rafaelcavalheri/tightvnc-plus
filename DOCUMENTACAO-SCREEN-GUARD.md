# Documentação — Proteção de Tela do TightVNC Server (Screen Guard)

> **Resumo:** implementação de uma proteção de tela visível ao usuário local
> (estilo TeamViewer) no TightVNC Server 2.8.81, com opção de ativar/desativar
> pela interface de configuração, empacotamento via Inno Setup e toolchain
> atualizada para Visual Studio 2022.

---

## 1. Visão geral

Quando um cliente remoto se conecta ao TightVNC Server, o usuário que está
fisicamente na máquina passa a ver um **aviso visual de proteção de tela**:

- **Overlay de tela cheia** — cobre todo o desktop virtual (alpha 255,
  opaco). Se existir um `tela.png` ao lado do `tvnserver.exe`, essa imagem é
  desenhada esticada sobre toda a área; caso contrário o overlay fica preto;
- **Banner central de alerta** — caixa branca com borda azul, logo
  (`logo.png`, com fallback para o ícone embutido) e a mensagem
  *"Computador em manutenção, não desligar!"*. O banner só é exibido quando
  **não** há `tela.png`, já que a imagem de tela cheia dispensa o aviso;
- **Cursor local oculto** — enquanto o guard está visível, todos os cursores
  do sistema são substituídos por um cursor transparente (`SetSystemCursor`),
  para que o usuário local não veja o ponteiro sendo movido remotamente. São
  restaurados com `SystemParametersInfo(SPI_SETCURSORS)` assim que o guard
  deixa de estar visível;
- **Tela preta (opcional)** — janela preta opaca que oculta completamente o
  desktop do usuário local (campo `blankScreenEnabled` na memória
  compartilhada; hoje só é acionada pelo modo de teste).

Ao desconectar o último cliente, o aviso desaparece automaticamente.

### Características importantes

| Característica | Detalhe |
|---|---|
| **Click-through** | Todas as janelas usam `WS_EX_TRANSPARENT` e `WS_EX_NOACTIVATE` — nunca roubam foco, teclado ou mouse. O controle remoto continua funcionando normalmente. |
| **Exclusão de captura** | `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` impede que as janelas do guard apareçam na captura de tela — o cliente remoto vê o desktop normal. **Requer Windows 10 versão 2004 ou mais recente**; em versões anteriores a chamada falha silenciosamente e o operador remoto passa a ver o overlay também. |
| **Auto-encerramento** | O guard monitora um *heartbeat* do servidor; se o servidor morrer, o guard se encerra em até 5 segundos. Sem clientes, encerra em 2 segundos. |
| **Modo serviço e aplicação** | Funciona com o servidor rodando como serviço do Windows (sessão 0) ou como aplicação comum. |
| **Configurável** | Checkbox *"Show screen guard"* na aba *Connection* do TightVNC Service Configuration. |

---

## 2. Arquitetura

### 2.1 Componentes

```
┌─────────────────────────────────────────────────────────────┐
│ tvnserver.exe (serviço ou aplicação)                        │
│                                                             │
│  TvnServer ── cria/atualiza ──► Memória compartilhada       │
│      │                          "Global\TightVNC Screen     │
│      │                          Guard State"                │
│      │                                                    │
│      ├─ afterFirstClientConnect() → startScreenGuard()     │
│      ├─ afterClientCountChanged() → publishScreenGuardState│
│      └─ afterLastClientDisconnect() → stopScreenGuard()    │
│                                                             │
│  Heartbeat (CreateTimerQueueTimer, 1 s)                     │
└─────────────────────────────────────────────────────────────┘
                            │ (CurrentConsoleProcess/
                            │  Process)
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ tvnserver.exe -screenguard (sessão interativa do usuário)   │
│                                                             │
│  ScreenGuardApplication                                     │
│    ├─ overlay window   (escurecimento)                      │
│    ├─ banner window    (aviso com IP do cliente)            │
│    └─ blank window     (tela preta, opcional)               │
│                                                             │
│  Timer de 500 ms: lê estado + heartbeat + auto-encerra      │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 Memória compartilhada

Nome: `Global\TightVNC Screen Guard State`

Estrutura (`ScreenGuardSharedData`, definida em
`tvnserver-app/ScreenGuardApplication.h`):

| Campo | Tipo | Descrição |
|---|---|---|
| `generation` | `volatile LONG` | Contador incrementado a cada publicação de estado |
| `serverHeartbeatMs` | `volatile ULONG` | Heartbeat do servidor (GetTickCount), atualizado a cada 1 s |
| `clientCount` | `volatile LONG` | Número de clientes autenticados |
| `lastClientAddress[64]` | `TCHAR[]` | IP do último cliente autenticado (terminado em NUL) |
| `blankScreenEnabled` | `volatile LONG` | Reservado para a política de tela preta |

**Protocolo de leitura** (lado do guard): reler até que `generation` não
mude durante a leitura (lock-free com `InterlockedExchangeAdd(..., 0)`).

**Segurança:** no modo serviço, o `CreateFileMapping` recebe DACL nulo
(`SetSecurityInfo(..., SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, 0,0,0,0)`)
para que o processo do guard na sessão do usuário (potencialmente sem
privilégios administrativos) consiga abrir o mapeamento.

### 2.3 Ciclo de vida do guard

1. Primeiro cliente autentica → `afterFirstClientConnect()` → se a opção
   estiver habilitada, `startScreenGuard()` inicia `tvnserver.exe
   -screenguard` na sessão interativa (`CurrentConsoleProcess` em modo
   serviço, `Process` em modo aplicação).
2. O guard abre a memória compartilhada, cria as três janelas e aplica o
   estado publicado (mostra overlay + banner quando `clientCount > 0`).
3. A cada 500 ms (`WM_TIMER` na janela overlay):
   - relê o estado e atualiza visibilidade/posição das janelas — é aqui que
     o cursor local é escondido (`clientCount > 0`) ou restaurado
     (`clientCount == 0`);
   - verifica o heartbeat (timeout de 5 s → shutdown);
   - se `clientCount == 0` por 4 ticks consecutivos (2 s) → shutdown.

   Note a ordem: a visibilidade é aplicada **antes** da contagem de ticks.
   Assim que o servidor publica `clientCount = 0`, o overlay some e os
   cursores voltam no primeiro tick (≤ 500 ms); os 2 s seguintes são apenas
   a carência para uma reconexão rápida antes de o processo encerrar.
4. Último cliente desconecta → `afterLastClientDisconnect()` →
   `stopScreenGuard()` publica `clientCount = 0`, aguarda até 3,5 s pela
   saída limpa (`SCREEN_GUARD_EXIT_POLLS`) e, se necessário, encerra o
   processo com `TerminateProcess`. O orçamento precisa ser maior que os 2 s
   de carência do guard, senão praticamente toda parada acabaria em
   `TerminateProcess` mesmo com o guard prestes a sair sozinho.
5. Troca de sessão ativa (troca rápida de usuário, sessão RDP) →
   `ensureScreenGuardInCurrentSession()` reinicia o guard na sessão nova.
   Antes de matar o processo da sessão antiga ele publica `clientCount = 0`
   e espera até 800 ms (`SCREEN_GUARD_ABANDON_POLLS`): sem isso a sessão
   abandonada — que pode continuar viva — ficaria **sem cursor do mouse**,
   porque `SetSystemCursor` vale para a sessão inteira e só é desfeito pela
   rotina de limpeza do próprio guard.
6. O heartbeat usa `CreateTimerQueueTimer` (thread pool) — **não exige**
   message pump, o que é necessário porque `startScreenGuard()` pode rodar
   na thread de um cliente RFB.

### 2.4 Configuração (checkbox na interface)

- **Config:** `ServerConfig::m_screenGuardEnabled` (padrão: `true`)
- **Registro:** valor `ScreenGuardEnabled` (chave `HKLM\SOFTWARE\TightVNC\Server`)
  — salvo/carregado pelo `Configurator`. Se ausente (versões antigas),
  assume habilitado (não gera falha de carga).
- **Serialização IPC:** campos adicionados a `serialize()`/`deserialize()`.
- **Interface:** checkbox *"Show screen guard"* (`IDC_SCREEN_GUARD`, 1098)
  criado **em runtime** no `ConnectionConfigDialog` — a aba *Connection*, que
  também é montada em runtime a partir de um `DLGTEMPLATE` em memória.
  Motivo: `tvnserver.rc` e `resource.h` estão em UTF-16; edição fora do
  Visual Studio é arriscada.
- **Aplicação em tempo real:** `TvnServer::onConfigReload()` inicia/encerra
  o guard conforme a nova configuração, mesmo com clientes conectados —
  **desde que a configuração chegue ao serviço em execução**, o que só
  acontece pela Control Interface (veja a seção 6).

---

## 3. Arquivos criados/modificados

### 3.1 Novos

| Arquivo | Descrição |
|---|---|
| `tvnserver-app/ScreenGuardApplication.h` | Classe `ScreenGuardApplication`, nomes da memória compartilhada e `ScreenGuardSharedData` |
| `tvnserver-app/ScreenGuardApplication.cpp` | Janelas de proteção, timer/watchdog, desenho do banner, modo de teste |
| `tvnserver/resource_screenguard.h` | `#define IDC_SCREEN_GUARD 1098` (header UTF-8 separado; não toca nos recursos UTF-16) |
| `build-tvnserver.bat` | Compila Release x64/x86 com detecção automática do Visual Studio |
| `build-and-package.bat` | Build completo + geração do instalador Inno Setup |
| `tightvnc-setup.iss` | Script do instalador Inno Setup |
| `diagnose-env.bat` | Diagnóstico do ambiente de build |

### 3.2 Modificados

| Arquivo | Mudança |
|---|---|
| `tvnserver-app/TvnServer.h/.cpp` | Membros e métodos do screen guard; start/stop/publish/heartbeat; integração nos eventos de conexão e no `onConfigReload` |
| `tvnserver-app/RfbClientManager.h/.cpp` | Novo `getAllClientsInfo()` (inclui clientes ainda em inicialização); notificação `afterClientCountChanged()` |
| `tvnserver-app/RfbClientManagerEventListener.h` | Novo evento virtual `afterClientCountChanged()` (implementação default vazia) |
| `tvnserver/tvnserver.cpp` | Roteamento dos modos `-screenguard` e `-screenguardtest` no entry point |
| `tvnserver-app/tvnserver-app.vcxproj(.filters)` | Registro dos arquivos novos no projeto |
| `server-config-lib/ServerConfig.h/.cpp` | `m_screenGuardEnabled` + getter/setter + serialização |
| `server-config-lib/Configurator.cpp` | Salvar/carregar `ScreenGuardEnabled` no registro |
| `wsconfig-lib/ConnectionConfigDialog.h/.cpp` | Aba *Connection* montada em runtime: checkbox "Show screen guard", handler, `updateUI`/`apply` |
| `wsconfig-lib/ConfigDialog.h/.cpp` | Registro da aba *Connection* no controle de abas |
| `viewer-core/viewer-core.vcxproj(.filters)` | Removidas referências órfãs `ExtendedDesktopSizeDecoder.*` |
| `rfb-sconn/rfb-sconn.vcxproj(.filters)` | Removidas referências órfãs `SetDesktopSize.*` |
| `screen-hooks/screenhooks.vcxproj` | `WindowsTargetPlatformVersion` 10.0.22621.0 → 10.0 (usa SDK instalado) |

---

## 4. Build

### 4.1 Pré-requisitos

- Visual Studio 2022 Build Tools (ou VS 2022 completo) com a carga
  **"Desenvolvimento para desktop com C++"** (inclui MSVC v143 e Windows SDK);
- Inno Setup 6+ (`ISCC.exe`).

### 4.2 Comandos

```powershell
cd C:\Users\rafael.cavalheri\Documents\tight\tightvnc

# Build completo + instalador
.\build-and-package.bat

# Ou passo a passo:
.\build-tvnserver.bat x64    # compila Release x64 (gera em Release\x64\)
.\build-tvnserver.bat x86    # compila Release Win32 (gera em Release\x86\)
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" .\tightvnc-setup.iss
```

Saída do instalador: `Output\tightvnc-2.8.81-setup.exe`

### 4.3 Observações de toolchain

- A solução original usa o toolset **v140_xp** (VS 2015 + Windows XP), que
  não existe no VS 2022. O script sobrescreve globalmente com
  `/p:PlatformToolset=v143` — sem editar os 40+ `.vcxproj`.
- Diretórios de saída reais do MSBuild: `Release\x64\` e `Release\x86\`
  (diferentes do layout `x64\Release\` esperado pelo WiX antigo).
- Os scripts `.bat` evitam blocos `if (...)` com `%ProgramFiles(x86)%`
  porque o parêntese no **valor** da variável quebra o parser do cmd.exe
  (erro clássico `\Microsoft foi inesperado neste momento`).

---

## 5. Instalador (Inno Setup)

O `tightvnc-setup.iss`:

- Instala `tvnserver.exe`, `tvnviewer.exe`, `screenhooks32/64.dll`,
  `hookldr.exe`, `sas.dll` e `LICENSE.txt`;
- Registra e inicia o serviço (`-reinstall -silent` + `-start -silent`);
- **Cria exceções no Firewall do Windows**:
  - regra por programa `TightVNC Server` (funciona para qualquer porta);
  - regra por porta `TightVNC Server (TCP 5900)`;
  - regra por porta `TightVNC Server (TCP 5800)` (Web Access);
- Na desinstalação: para/remove o serviço e remove as regras de firewall.

---

## 6. Configuração da proteção de tela

1. Abra o **TightVNC Service Configuration** pela **Control Interface** —
   ícone da bandeja do TightVNC, ou o atalho *"TightVNC Server - Control
   Interface"* no Menu Iniciar (`tvnserver.exe -controlservice`).
2. Vá na aba **Connection**.
3. Marque/desmarque **"Show screen guard"**:
   - **Marcado (padrão):** usuário local vê a proteção quando alguém conecta;
   - **Desmarcado:** a tela do usuário local permanece livre durante o
     controle remoto.
4. Clique **Apply** — a mudança vale imediatamente, sem reiniciar o serviço e
   sem derrubar a sessão remota em andamento.

> **Importante — use a Control Interface, não a Offline Configuration.**
> O atalho *"TightVNC Server - Offline Configuration"* (`-configservice`)
> abre o mesmo diálogo, mas ele **não conversa com o serviço em execução**:
> apenas grava no registro e avisa que a mudança vale a partir do próximo
> início do serviço. Só a Control Interface envia a configuração pelo pipe
> de controle (`SET_CONFIG` → `Configurator::load()` → `notifyReload()` →
> `TvnServer::onConfigReload()`), que é o que liga/desliga o guard na hora.

### 6.1 Comportamento ao alternar a opção com um cliente conectado

| Ação | O que acontece | Quando |
|---|---|---|
| Marcar + **Apply** | `onConfigReload()` vê a opção habilitada, o guard parado e a lista de clientes não vazia → `startScreenGuard()` lança o processo e publica o `clientCount` real | proteção aparece em ~0,1–0,5 s |
| Desmarcar + **Apply** | `onConfigReload()` → `stopScreenGuard()` publica `clientCount = 0` | overlay some e o cursor local volta no primeiro tick (≤ 0,5 s); o processo encerra depois, em até ~2 s |
| Marcar de novo + **Apply** | o `stopScreenGuard()` anterior zerou o estado do processo, então `startScreenGuard()` roda de novo e republica o `clientCount` | proteção reaparece em ~0,1–0,5 s |

O ciclo pode ser repetido quantas vezes for necessário. Habilitar a opção
**sem nenhum cliente conectado** não mostra nada — é o comportamento
esperado: `onConfigReload()` só inicia o guard quando há cliente, e a
proteção passa a aparecer normalmente na próxima conexão.

---

## 7. Testes e diagnóstico

### 7.1 Teste manual do guard (sem cliente)

```powershell
# Modo normal (depende do serviço rodando e de cliente conectado)
& "C:\Program Files\TightVNC\tvnserver.exe" -screenguard

# Modo de teste (força exibição do overlay + banner + tela preta)
& "C:\Program Files\TightVNC\tvnserver.exe" -screenguardtest
```

O modo de teste ignora `clientCount` e nunca se auto-encerra — ideal para
validar as janelas. Para fechar, encerre o processo (`taskkill /IM
tvnserver.exe /F` **não** — use o Gerenciador de Tarefas para o processo
correto, pois o serviço usa o mesmo nome).

> **Atenção:** no modo normal, rodar `-screenguard` sem clientes conectados
> faz o guard sair em ~2 segundos (comportamento esperado — por isso "não
> acontece nada" visualmente).

### 7.2 Teste ponta a ponta

1. No PC remoto: confirme que o serviço está ativo (`Get-Service tvnserver`).
2. No PC cliente: `Test-NetConnection -ComputerName <IP> -Port 5900`.
3. Conecte com o `tvnviewer.exe` (`<IP>::5900`).
4. No PC remoto: a proteção (imagem `tela.png` em tela cheia ou, na falta
   dela, o overlay preto + banner) deve aparecer imediatamente após a
   autenticação, e o cursor local deve sumir.
5. Desconecte: a proteção some e o cursor local volta em ≤ 0,5 s.

### 7.2.1 Teste do liga/desliga em tempo real

Com um cliente já conectado, na máquina remota:

1. Abra a **Control Interface** (ícone da bandeja) → aba **Connection**.
2. Marque **"Show screen guard"** → **Apply** → a proteção aparece na
   máquina local.
3. Desmarque → **Apply** → a proteção some (e o cursor local volta).
4. Marque de novo → **Apply** → a proteção volta a aparecer.

Se nada acontecer, confira se o diálogo foi aberto pela Control Interface e
não pela Offline Configuration (veja a seção 6).

### 7.3 Logs

O log do servidor registra o ciclo do guard:

| Mensagem | Significado |
|---|---|
| `Screen guard shared memory has been initialized` | Memória compartilhada OK |
| `Starting the screen guard application` | Guard sendo iniciado |
| `Failed to start the screen guard application: ...` | Falha ao subir o processo (ver erro) |
| `Stopping the screen guard application` | Último cliente desconectou, ou a opção foi desmarcada na configuração |
| `Screen guard process exited; restarting it` | Guard morreu sozinho com cliente conectado — está sendo relançado |
| `Screen guard session changed from N to M; restarting it` | Sessão interativa mudou — guard sendo movido para a sessão nova |
| `The screen guard application did not exit in time, terminating it` | Fallback de encerramento forçado (após 3,5 s) |

Local típico: `C:\ProgramData\TightVNC\tvnserver.log` (ou junto ao exe).

### 7.4 Problemas conhecidos

| Sintoma | Causa provável | Ação |
|---|---|---|
| Cliente não conecta (timeout) | Firewall do Windows | Reinstalar (o `.iss` cria as regras) ou `netsh advfirewall firewall add rule ...` manual |
| Guard não aparece com cliente conectado | Opção desmarcada na configuração; ou `tvnserver.exe` desatualizado (serviço não foi atualizado na instalação) | Verificar checkbox; parar o serviço e substituir o exe |
| Marcar/desmarcar a opção não surte efeito na hora | Diálogo aberto pela *Offline Configuration* (`-configservice`), que só grava no registro | Usar a *Control Interface* (`-controlservice`), ou reiniciar o serviço |
| Guard fecha sozinho em 2 s sem clientes | Comportamento normal | — |
| Guard aparece também para o operador remoto | `WDA_EXCLUDEFROMCAPTURE` exige Windows 10 2004+; em sistemas anteriores a exclusão de captura não funciona | Atualizar o Windows da máquina remota; até lá, desmarcar a opção antes de trabalhar remotamente nessas máquinas |

---

## 8. Limitações e evolução futura

- **Tela preta (`blankScreenEnabled`)** está estruturada na memória
  compartilhada, mas ainda não há checkbox dedicado na interface — o
  comportamento atual mostra o aviso + escurecimento, não a tela preta
  total.
- Antes do Windows 10 versão 2004 não há `WDA_EXCLUDEFROMCAPTURE` — o
  overlay aparece também na captura remota. Como hoje a proteção é uma
  imagem opaca de tela cheia (`tela.png`), isso deixa o operador remoto sem
  enxergar o desktop, inclusive o próprio diálogo de configuração.
- O guard roda na sessão ativa do console; em sessões RDP desconectadas o
  aviso não é visível até o usuário se reconectar (o processo é por sessão).
- Sincronização por memória compartilhada usa protocolo lock-free com
  `generation` — suficiente para a carga atual; para múltiplos monitores
  com troca de resolução, o guard reposiciona as janelas a cada mudança de
  geração.
