# FarSightVNC — UI fase 1 (layout): relatório

Versão: v1.1.0a. Branch: `feature/ui-layout`. Plano: `docs/ui/plano-layout.md`.
Commits: `git log 4114e36..HEAD` (23 commits, da refatoração `SessionHost` ao botão "+").

## O que mudou

- **Shell** (`ShellWindow`): moldura própria que hospeda a sessão, com `TabModel` pronto para várias abas (hoje uma por janela). Tela cheia, DPI e reconexão passam pelo `SessionHost`.
- **Barra de título própria**: logo, aba, engrenagem de Configurações, minimizar/maximizar/fechar. O botão de maximizar mantém o Snap Layouts do Windows 11. A aba mostra só o nome da sessão.
- **Barra de sessão** em Direct2D no lugar da toolbar comctl (menu "⋯" mantido).
- **Pílula de tela cheia** flutuante, desenhada com `ChromeRenderer`. A janela de tela cheia agora é de fato topmost.
- **Ajuste sempre ligado**: a tela remota sempre cabe na janela, proporção mantida, sem barras de rolagem nunca. A opção `-scale=` é ignorada.
- **Home**: tela inicial no lugar do diálogo de login (endereço, recentes, favoritos, modo escuta).
- **Configurações** dentro da janela: Aparência, Conexão, Entrada, Exibição, Geral, Atualizações e Sobre. Os diálogos antigos foram removidos.
- **Botão "+"** na barra de título, logo após a aba: abre uma nova janela na Home (igual a Ctrl+N), com dica "New connection (Ctrl+N)". É o primeiro ícone Lucide (`plus`, lucide-static 0.468.0, ISC), desenhado como SVG por `ID2D1DeviceContext5`, com o glifo da fonte de ícones do sistema como reserva. O aviso de licença está em Configurações > Sobre e nos arquivos `LEIAME.txt`/`README.txt`.
- **Correção do teclado** (branch `fix/keyboard-ctrl-shortcuts`, já integrada): a soltura de uma tecla segue o caminho da sua pressão (Ctrl/Alt não ficam presos, Ctrl+C/V voltam a funcionar) e Print Screen fica local. O `onHookProc` manteve as duas regras: `WinHooks::routeKey` e a posse do teclado pela shell, sem teclas ao servidor durante as Configurações. A fusão não precisou de correção.

## Testes (x64, Release; GDI e GPU)

- Unitários: viewer-keyboard 480/0, viewer-input-queue 39/0, viewer-favorites 88/0, updater-test 2103/0.
- `smoke-test` 4/4, `phase2-test -DisplayModes` 194/0 e `keyboard-e2e-test` 39/0, ambos com GDI e com `FARSIGHTVNC_RENDERER=gpu`, só em 127.0.0.1 contra servidor falso.
- Builds: Release e Debug x64 (MSBuild) e preset CMake `x64-release`. Aviso C4244 preexistente em `ViewerWindow.cpp:1470` (vindo da correção do teclado).
- Capturas do botão "+" (normal, hover, pressionado) e teste de hit-test (o "+" é cliente, a legenda vazia arrasta, o maximizar devolve `HTMAXBUTTON`). Clique no "+" abriu uma segunda janela.

## Não verificado

- Windows 10 (só o Windows 11 foi usado); o caminho de reserva sem SVG não foi exercitado.
- Mudança real de DPI e monitores múltiplos.
- Teclado e mouse físicos (os testes enviam mensagens, nunca `SendInput`).
- Leitor de tela.
- Conexão com TightVNC Server real nesta versão e uso fora de 127.0.0.1.

## Próximos passos

1. Trocar os glifos restantes pelos ícones Lucide (ícones da barra de sessão, da pílula, da engrenagem e dos botões de janela).
2. Temas Auto/Claro/Escuro (os tokens escuros e a opção de Aparência, hoje só gravada).
3. Interface em português fica adiada.
4. Abas de verdade (o "+" passa a abrir uma aba em vez de uma janela).
