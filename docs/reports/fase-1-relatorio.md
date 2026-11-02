# Fase 1 — teclado

Data: 2026-11-01. Integração/revisão: Codex, a partir do trabalho incompleto
em `FarSightVNC-wt/fase-1`. Implementação na branch `develop`; os worktrees
dos Claudes foram preservados.

- Press/release usa scancode + extended e conserva o keysym original.
- Shift direito usa scancode 0x36. Autorepeat repete o keysym armazenado;
  modificadores repetidos não geram transições extras.
- ToUnicodeEx consulta sem alterar o estado (bit 2), antes de TranslateMessage.
  O loop entrega imediatamente os caracteres da composição antes da próxima tecla.
- Hook respeita foco/foreground, preserva scancode inteiro e ignora injetados.
  Modificadores físicos compartilham a fila; locks permanecem a cargo do sistema.
- Perda de foco libera também teclas comuns e modificadores da toolbar.
- Alt/Meta conserva o símbolo de release mesmo se Scroll Lock mudar.
- Log de diagnóstico de VK/dados usa o nível debug existente.

Validação: 188 testes por configuração, zero falhas em Release/Debug x64/Win32.
O harness não injeta entrada no desktop e não conecta ao serviço VNC instalado.
Resultados: `build-logs/keyboard-*.txt`. Build do Viewer nas quatro configurações:
`build-logs/phase1-viewer-build.txt`.

Limitações: o harness simula a parte relevante do loop de mensagens; não comprova
interoperabilidade com todos os servidores nem hook real sob carga. IME japonês,
Windows anterior ao suporte ao bit 2 de ToUnicodeEx (Windows 10 1607), e uso
prolongado ainda não validados. Não há opção de voltar ao tradutor antigo.
O teste de teclado não é um teste de toda a aplicação.

Próxima fase: 2 — DPI, janela e monitores. O histórico de commits e este relatório
são o ponto de continuidade; não sobrescrever develop com os snapshots antigos.

## Pendências da Fase 1 (2026-11-02)

### Revisão de código (`a3640bc..8307444`)

Corrigidos, cada um com teste de regressão em `viewer-keyboard-test` (R1-R3):

- R1: Win key segurada no servidor não era solta se o flag "ignorar Win" fosse
  ligado antes do release (sair de tela cheia com Win pressionada). Agora o
  release passa quando a tecla está registrada como pressionada.
- R2: em tela cheia somente-visualização o hook engole Ctrl/Alt/Shift e
  `onKey` não atualizava o estado de teclado da thread; os aceleradores
  Ctrl+Alt+Shift+letra (usam `GetKeyState`) deixavam de funcionar. A
  atualização agora ocorre também em modo view-only, via
  `WinHooks::applyKeyToStateTable` (testável). As flags `m_ctrlDown/m_altDown`
  (trava Ctrl/Alt da barra de ferramentas) voltam a mudar só em eventos de
  Ctrl/Alt, em vez de a cada tecla (regressão do commit 8307444; coberto só por
  leitura de código, `DesktopWindow` não é testável sem janela).
- R3: o hook é global ao processo, mas `unregisterKeyboardHook` ignorava o
  argumento e `~WinHooks` removia o hook sempre; fechar outra janela do viewer
  derrubava o hook da janela em tela cheia. Agora só o dono remove o hook.
  Também há guarda contra listener nulo.
- Cabeçalho `Modified for FarSightVNC` ausente em `DesktopWindow.cpp` e
  `TvnViewer.cpp`: adicionado. `MODIFICATIONS.md` estava correto.

Suspeitas não confirmadas (código não alterado):

- Ao perder o foco a tabela de teclado da thread (`SetKeyboardState`) mantém
  modificadores como pressionados até o Windows ressincronizá-la; não foi
  possível observar sem entrada real.
- O Ctrl falso do AltGr pode vir do hook com bit estendido em alguns teclados;
  nesse caso colidiria com o Ctrl direito em `m_pressedKeys` (chave 0x11D).
- Em tela cheia o hook engole Alt+Tab/Win também em modo view-only (igual ao
  código antigo, não é regressão).
- O mapa `m_pressedKeys` é limitado (uma entrada por tecla física) e esvaziado
  na perda de foco: sem crescimento ilimitado.
- O loop de `TvnViewer.cpp` só muda a ordem para janelas com a propriedade
  `FarSightVNC.RemoteKeyboard`; diálogos modais/modeless e aceleradores seguem
  a ordem original.

### Prova fail-before

Driver descartável (não versionado) compilado contra `rfb/RfbKeySym.cpp` de
`a3640bc`, com um loop que emula o viewer antigo (TranslateMessage antes do
dispatch, ToUnicodeEx com flags 0) e a camada ABNT2/US/US-Intl real:

| Defeito | Reproduziu no código antigo? | Evidência |
|---|---|---|
| H1 release assimétrico de `?` | Sim | ABNT2 Shift+`?`, Shift solto antes: `+Shift_L +3f -Shift_L -2f` (solta `/`, `?` fica presa) |
| H2 estado do kernel / ToUnicodeEx | Parcial | US-Intl dead acute duas vezes envia 1 char (`'`) onde o Windows entrega 2; todo release após dead key gera release espúrio (`-61`, `-20`) e desbalanceia. A pior forma (hook com TranslateMessage sem modificadores) só por leitura de código |
| H3 Shift direito como Shift_L | Sim | Shift direito (scan 0x36, VK_SHIFT genérico): `+ffe1 -ffe1` (Shift_L) em vez de Shift_R |
| H5 foco perdido com tecla comum | Sim | `+61` sem release; com Shift: `+Shift_L +A -Shift_L` (A presa) |
| H4 máscara de scancode | Só por leitura de código | `(scanCode & 0xf) << 16`: 0x73 vira 0x03, 0x36 vira 0x06. O código antigo ignorava o scancode, portanto era latente; seria fatal para a detecção do Shift direito (0x36) |
