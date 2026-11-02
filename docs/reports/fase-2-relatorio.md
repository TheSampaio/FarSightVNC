# Fase 2 — DPI, janelas e monitores

Data: 2026-11-02. Branch `feature/fase-2-dpi-monitores` (local, sem push), sobre
`feature/fase-1-teclado` (31b8a92). Base de trabalho: snapshot do Codex (1619a42),
revisado de forma independente e corrigido.

## Reconciliação (setup)

- Alterações pendentes guardadas em `stash@{0}` ("F2 pendente antes da reconciliacao");
  o stash não foi descartado. `stash@{1}` é da Fase 1 e ficou intacto.
- Rebase sobre `feature/fase-1-teclado`: o commit 793b29e (harness) já existia no destino
  (mesmos arquivos, patch idêntico) e foi descartado pelo próprio git por ficar vazio.
- Comparação stash x HEAD: o stash é um subconjunto anterior do que 1619a42 já contém
  (mesmos `DpiUtils`, manifesto, `ToolBar::setDpi`, `ScrollBar`, `FileInfoListView`,
  `WM_DPICHANGED`). HEAD é melhor em tudo: tratamento de falha de GDI em `setDpi`,
  roda horizontal e bloqueio de X1/X2, menu Display, `getBorderSize` por DPI, hook
  da Fase 1. Única diferença útil no stash (`#define WM_MOUSEHWHEEL`) já existe em
  `DpiUtils.h` de HEAD. Nada foi portado.
- Descartados: a alteração de `tools/build.ps1` (corrigida em 04a1798) e
  `docs/reports/fase-2-handoff.md` (relatório de passagem obsoleto: dizia "nada
  confirmado" e "harness não rodou"; este relatório o substitui).

## Revisão de 1619a42 — defeitos corrigidos

| Commit | Defeito confirmado |
|---|---|
| fix(dpi) | `onDpiChanged` não guardava a nova geometria em `m_rcNormal` (sair do fullscreen restauraria tamanho no DPI antigo) e desreferenciava `lParam` sem checar. |
| fix(pointer) | `setNewFramebuffer` comparava o tamanho real novo com o DIB alinhado a 4 (1366x768 -> 1368x768): redesenho total desnecessário. Agora usa `m_serverDimension`. |
| fix(window) | `calculateDefaultSize` testava só o desktop remoto contra a área de trabalho; com moldura + toolbar a janela passava da área e era centralizada parcialmente fora da tela (ex.: 1200x1000 -> janela em y=-19..1051 numa área de 1032). |
| fix(fullscreen) | Escolher um monitor no menu Display, com a janela em modo normal, só guardava a escolha. Agora entra em fullscreen. A lista de monitores era calculada uma vez; agora acompanha `WM_DISPLAYCHANGE` e um monitor que sumiu sai da seleção. |
| test(display) | `phase2-test.ps1 -DisplayModes` e lista de linhas FAIL ao final. |

Sem defeito encontrado: vazamentos de GDI em `ToolBar::setDpi` (todos os caminhos liberam
bitmap/DC/lista de imagens; a lista antiga é destruída), arredondamento de borda em
`ScaleManager` (último pixel mapeia para fb-1; há clamp no `getViewerCoord`),
cabeçalhos `// Modified for FarSightVNC on ...` (presentes em todos os arquivos de código
alterados; `dpi-aware.manifest` e `DpiUtils.*` são novos) e linhas de `MODIFICATIONS.md`.
Observação não corrigida (pré-existente do TightVNC): `m_workArea` (WINDOWPLACEMENT) não
tem `length` definido antes de `GetWindowPlacement`.

## Itens do escopo

1. **PerMonitorV2 / WM_DPICHANGED:** manifesto embutido (`PerMonitorV2,PerMonitor`,
   Common-Controls 6 preservado). Harness: contexto da janela = PerMonitorV2, DPI 96.
   O handler só foi exercitado por inspeção; não houve mudança real de DPI.
2. **Original / Ajustar / Personalizado:** menu Display (WM_SYSCOMMAND 0xA820/0xA830/0xA840)
   reaproveita `commandScale100`, `fitWindow` e o diálogo de opções existentes; nenhuma
   opção removida. Harness: Fit e Original mudam o modelo de ponteiro e os cantos batem.
   "Custom scale..." apenas abre o diálogo existente e não foi acionado no teste.
3. **Ponteiro com dimensões reais:** 1366x768 e 1366x769, 100%/50%/Auto: cantos exatos.
   O binário oficial 2.8.90 falha em 1366x768 (top-left, bottom-right, auto): evidência
   de fail-before em `build-logs/phase2-official.txt`.
4. **Fullscreen com escolha de monitor / todos os monitores:** teste com um monitor
   (1920x1080): "todos" cobre a tela virtual 0,0,1920,1080, "atual" cobre o monitor,
   sem legenda; saída restaura a janela com legenda.
5. **Monitores verticais:** não há monitor vertical. Coberto: desktop remoto vertical
   1080x1920 em monitor horizontal (Fit e 100%), e posicionamento de desktops altos na área
   de trabalho (correção acima, com fail-before).
6. **Roda horizontal e X1/X2:** `WM_MOUSEHWHEEL` -> botões RFB 6/7 (máscaras 32/64):
   os 12 FAIL do binário oficial passam no novo (200/200). X1/X2 **permanecem desativados**:
   o botão X2 seria o botão 9 (bit 8), que não cabe na máscara de 8 bits do PointerEvent, e
   não há negociação de pseudo-encoding (-316) nem mapeamento confirmado nos servidores;
   por isso nada é enviado (o harness confirma que nenhuma máscara indefinida sai).

## Evidência

Build (rebuild limpo) de `tvnviewer` e `viewer-keyboard-test`: Release/Debug x x64/Win32, 0 erros
(avisos legados). `viewer-keyboard-test`: 200 passam / 0 falham nas 4 configurações.
`keyboard-e2e-test.ps1` (porta 5922): 10 casos, 0 falhas. `phase2-test.ps1 -DisplayModes`
(1920x1080, 1080x1920, 1366x768, 1366x769 x 100/50/auto, porta 5922): 200 PASS / 0 FAIL
em Release x64, Release Win32 e Debug x64. Binário oficial: 51 PASS / 15 FAIL (12 de roda
horizontal mais 3 de borda em 1366x768). Os testes usam apenas PostMessage nas janelas do
próprio viewer e servidor falso em 127.0.0.1; HKCU\...\History restaurado, nenhum processo
remanescente. Nota: o build incremental reutilizou um binário antigo depois de um teste
fail-before (mtime preservado ao restaurar fontes); a verificação final usou rebuild limpo.

## Não verificado

- DPI 125/150/200% e troca de monitor com `WM_DPICHANGED`: a máquina roda a 96 DPI e
  PostMessage não simula mudança de DPI. Escala real de toolbar, diálogos (dependem do
  escalonamento automático do Windows para diálogos PMv2) e barras de rolagem não foram vistos.
- Vários monitores: escolha de um monitor específico, tela virtual com mais de um monitor,
  desconexão de monitor e `WM_DISPLAYCHANGE` real.
- Monitor em orientação retrato e 3840x2160.
- "Custom scale..." (diálogo) e o fluxo visual do menu (os comandos foram postados).
- Ausência de vazamento de GDI após muitas trocas de DPI (revisão de código apenas).
- Servidores de terceiros e hook de teclado real em fullscreen sob uso.
