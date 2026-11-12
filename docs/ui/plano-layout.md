# FarSightVNC — Redesign de UI, fase 1: layout

Decisões do dono (2026-11-11):
- Interface moderna e minimalista, no estilo AnyDesk e RustDesk.
- Ícones: Lucide, monocromáticos. Nada dos ícones coloridos do VNC clássico.
- Temas Auto/Claro/Escuro.
- Ordem: **layout → ícones → temas**.
- Uma seção de **Configurações** para o usuário.
- Estrutura pronta para **abas no estilo navegador** no futuro. As abas em si ficam fora desta fase.
- "Ajustar à janela" continua ligado por padrão.

Base técnica: continua Win32/C++20. A chrome é desenhada com Direct2D/DirectWrite (bibliotecas do sistema). Não entram dependências novas; os SVGs do Lucide chegam na fase 2.

## Arquitetura atual (resumo)

- `main.cpp` tem três entradas: modo escuta, conexão pela linha de comando ou `LoginDialog`. `TvnViewer` é uma janela invisível que só bombeia mensagens (timers, `WM_USER_*`).
- Cada conexão vira um `ViewerInstance` (`TvnViewer::runInstance`), que contém por valor `ViewerWindow` (janela de topo `TvnWindowClass`), `DesktopWindow` (filha, também `TvnWindowClass`), `RemoteViewerCore` e o socket.
- Tempo de vida: `WM_NCDESTROY` liga `m_stopped`; `ViewerCollector::deleteDeadInstances` apaga a instância (timer de 50 ms). A reconexão cria uma instância e uma janela novas.
- Todas as janelas rodam na thread principal. O core fala com elas só por `postMessage`.
- Menus: `ViewerMenu` fica no menu de sistema. Os aceleradores passam por `TvnViewer::processMessages`.
- Toolbar: comctl com `IDB_TOOLBAR`.
- Gancho de teclado: registrado em `WM_ACTIVATE`. `onHookProc` exige `GetForegroundWindow()==getHWnd()`.
- Tela cheia: muda o estilo da própria janela de topo. `FullScreenToolbar` é um popup comctl.
- DPI: PerMonitorV2; `onDpiChanged` está em `ViewerWindow`. `util/winhdr.h` força `WINVER 0x0501`, então as APIs novas vêm do padrão do `ReconnectDialog` ou de `GetProcAddress`.
- GPU: a swap chain fica no HWND do `DesktopWindow`; d3d11/d2d1 são carregados em tempo de execução.
- Os testes dependem das classes de janela: `keyboard-e2e-test` e `phase2-test` procuram a janela de topo `TvnWindowClass` e o primeiro descendente `TvnWindowClass`; `phase2-test` manda `WM_SYSCOMMAND 0xA830` e `WM_COMMAND 215` para a janela de topo; `smoke-test` lê `MainWindowTitle`.

## Arquitetura alvo

Arquivos novos em `tvnviewer/shell/` e `gui/d2d/`, todos com cabeçalho GPLv2+.

- **`ShellWindow`** (classe **`TvnWindowClass`**, para preservar os testes): a moldura.
  - Barra de título própria: DWM, `WM_NCCALCSIZE`, `WM_NCHITTEST` com `HTMAXBUTTON` para o Snap Layouts.
  - `TabModel` com um vetor de `ShellTab`; hoje existe 1 aba.
  - Cuida de tela cheia, DPI, `WM_ACTIVATE` e repasse de `WM_COMMAND`/`WM_SYSCOMMAND` para a aba ativa.
- **`ShellTab`** (interface), com três implementações:
  - `HomeTab`: dono da `HomePage`.
  - `SessionTab`: referencia o `ViewerInstance`, mas não é o dono.
  - `SettingsTab`: dono da `SettingsPage`.
- **`SessionHost`** (interface implementada pela shell): `hostHwnd`, `setTitle`, `enterFullscreen`/`exitFullscreen`, `requestClientSize`, `onSessionEnded`, `rootHwnd`.
- **`TitleBar`**: logo, abas, engrenagem de Configurações, minimizar/maximizar/fechar.
- **`SessionToolbar`**: barra D2D fina com grupos.
  - Exibição: ajustar, 100%, tela cheia, monitor.
  - Entrada: Ctrl+Alt+Del, Win, menu de teclas especiais.
  - Área de transferência e transferência de arquivos.
  - Menu "⋯" com o `ViewerMenu` atual.
  - À direita, a área de conexão com placeholders de latência e cadeado.
- **`ChromeRenderer`** (`gui/d2d/`): fábricas D2D/DWrite, fontes e `drawGlyph()`. Nesta fase usa texto como placeholder; na fase 2 passa a SVG via `ID2D1DeviceContext5`, que é opcional no Win10 1703+.
- **`ThemeTokens`** (`gui/d2d/ThemeTokens.h`): cores, raios, alturas e espaçamentos. Por enquanto só os valores claros; a fase 3 acrescenta os escuros.
- **`HomePage`**: tela inicial no lugar do `LoginDialog`.
  - Campo de endereço nativo (por IME e acessibilidade) e botão Conectar.
  - Recentes e favoritos (`ConnectionFavorites::buildHostList`).
  - Acesso ao modo escuta.
- **`SettingsPage`**: página dentro da janela, com navegação lateral. Seções:
  - Aparência: `ViewerConfig::theme` = Auto/Claro/Escuro. Fica salvo agora e passa a ter efeito na fase 3.
  - Conexão padrão e Entrada: reaproveitam as `OptionsPage`.
  - Exibição/Renderizador: grava `Renderer`.
  - Geral: campos do `ConfigurationDialog`.
  - Atualizações: `UpdateController`.
  - Sobre.

Mudanças em classes existentes:
- `ViewerWindow` vira **filha** da shell, com classe nova `FsvSessionView`. O `DesktopWindow` continua `TvnWindowClass`.
- Tela cheia, título, DPI e registro do gancho saem da `ViewerWindow` e passam pelo `SessionHost`. `onHookProc` passa a comparar com `GetAncestor(hwnd, GA_ROOT)`.
- `TvnViewer::runInstance(conData, conf, ShellWindow *target = nullptr)` cria uma shell nova quando não recebe destino. A reconexão reaproveita a mesma shell (`WM_USER_RECONNECT` leva o HWND).
- O app encerra quando não há instâncias nem shells e o modo escuta está desligado.
- Quando a sessão termina, a shell volta para a Home. Fechar a shell para a sessão.
- Até as abas existirem, cada conexão (inclusive as recebidas no modo escuta e Ctrl+N) abre uma `ShellWindow` própria.
- As Configurações substituem temporariamente o conteúdo da aba ativa; a sessão continua em segundo plano.

## Fatias (um commit atômico cada)

Toda fatia verifica:
- O build `tools/build.ps1`.
- Os testes viewer-keyboard, viewer-input-queue e viewer-favorites.
- smoke, phase2 e keyboard-e2e, só em 127.0.0.1, portas 5921–5935. Nunca 5900/5800 nem o tvnserver instalado.
- A partir da fatia 3, também com `-renderer=gpu`.

1. **refactor**: interface `SessionHost` + `LegacyFrameHost` com o comportamento atual (sem mudança visível).
2. **feat**: `ThemeTokens` + `ChromeRenderer` (d2d1/dwrite/dwmapi no vcxproj e no CMake), ainda sem uso.
3. **feat**: `ShellWindow` com moldura nativa, hospedando o `ViewerWindow` filho. Inclui gancho, foco, aceleradores, `WM_COMMAND`/`SYSCOMMAND` e a regra de encerramento. É a fatia crítica.
4. **feat**: tela cheia e DPI na shell; reconexão na mesma shell.
5. **feat**: barra de título própria e aba única. *Revisão visual do dono.*
6. **feat**: `SessionToolbar` D2D no lugar da toolbar comctl. *Revisão visual.*
7. **feat**: pílula flutuante de tela cheia (`FullScreenToolbar` reescrita; `FullScreenToolbarLogic` fica como está). *Revisão visual.*
8. **feat**: `HomePage` como tela inicial. *Revisão visual.*
9. **feat**: esqueleto da `SettingsPage` com as seções Conexão e Entrada.
10. **feat**: seções Geral, Renderizador, Atualizações, Sobre e Aparência (tema salvo). *Revisão visual.*
11. **docs**: `MODIFICATIONS.md` e relatório. Depois, **chore**: bump para v1.1.0a.

Todo arquivo alterado recebe a linha "Modified for FarSightVNC" e uma entrada no `MODIFICATIONS.md`.

## Riscos

- **Gancho e foco:** só a janela de topo recebe `WM_ACTIVATE`, então a shell repassa. A toolbar responde `MA_NOACTIVATE` e devolve o foco ao `DesktopWindow`.
- **D2D da chrome junto com a swap chain filha:** usar `WS_CLIPCHILDREN` e testar no modo GPU com redimensionamento rápido.
- **Barra de título própria:**
  - Compensar o frame quando a janela está maximizada.
  - Margem DWM `top=1` no Win10.
  - Para o Snap Layouts, tratar à mão `WM_NCLBUTTONDOWN/UP` e `WM_NCMOUSEMOVE/LEAVE`.
  - Manter `WS_OVERLAPPEDWINDOW`.
- **Tela cheia:** margens DWM 0. Repetir `phase2-test -DisplayModes`.
- **DPI:** `WM_DPICHANGED` chega à shell, que recria as fontes e propaga para as filhas.
- **Tema escuro em controles Win32 nativos** (fase 3): difícil. As páginas novas devem usar o mínimo possível de controles nativos.
