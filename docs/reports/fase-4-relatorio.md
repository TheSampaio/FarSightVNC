<!-- Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later. -->
# FarSightVNC — Relatório da Fase 4 (interface)

- **Data:** 2026-11-03
- **Branch:** `feature/fase-4-ui-ux`, rebaseada sobre `feature/fase-5-estabilidade-seguranca` (`a903bbc`). A PR desta fase inclui a Fase 5 (PR #5) até ela ser mesclada.
- **Executado por:** Claude (gestor) e um subagente Claude por etapa, em sequência (todas mexem em `tvnviewer.rc` e `resource.h`).
- `fase-4-relatorio-parcial.md` fica como histórico da tentativa anterior.

## 1. Situação por item

| Item | Situação | Commits |
|---|---|---|
| 4.2 Opções em abas | Feito | `d6e91bc` |
| 4.3 Recentes e favoritos | Feito | `d2e750c`, `cbfd3e3` |
| 4.1 Toolbar flutuante em tela cheia | Feito | `8c20d4a`, `b0f364b` |
| 4.4 Estado da conexão e reconexão assistida | Feito | `7f8db11`, `52e2f77` |
| Marca FarSightVNC | Feito | `525ab02` |

## 2. O que mudou

- **4.2:** o diálogo "Connection Options" tem as abas Display (10 controles), Input (3) e Network (10). São os mesmos 23 controles do original, com os mesmos IDs e o mesmo código de leitura/gravação. Corrigidos no trabalho anterior: ordem de Tab, mnemônicos repetidos, grupos de rádio sem tab stop, Ctrl+Tab com dois diálogos abertos. **Sem aba Security:** `ConnectionConfig` não tem opção de segurança. Usa `uxtheme.lib` (biblioteca do Windows) para o fundo das abas.
- **4.3:** a lista de hosts do login mostra favoritos e depois recentes (mais recente primeiro; variantes de maiúsculas aparecem uma vez só). Caixa "Favorite host" e botão "Remove recent". Favoritos em `HKCU\Software\FarSightVNC\Viewer\Favorites` (criada só no primeiro uso, invisível para o TightVNC Viewer); recentes continuam em `HKCU\Software\TightVNC\Viewer\History`, respeitando o limite existente.
- **4.1:** janela pequena sempre no topo, sem foco (`WS_EX_NOACTIVATE`), no centro superior do monitor da tela cheia (ou do monitor do ponteiro no modo todos os monitores), com DPI do monitor. Botões: sair da tela cheia, Ctrl+Alt+Del, Opções, fixar, desconectar. Some após ~2 s; volta quando o ponteiro toca os 3 px do topo. Não há botão "somente visualização" porque não existe esse comando no viewer.
- **4.4:** o título mostra "(connecting...)" ou "(disconnected)" e a toolbar de tela cheia tem um rótulo de estado. Em queda inesperada aparece um diálogo com Reconnect / Stop retrying / Close e contagem regressiva; tentativas automáticas em 2, 4, 8 e 16 s (máx. 4), reutilizando host, opções e a senha só em memória. Sem reconexão após fechar pelo usuário, falha de autenticação ou modo listening. Política em `ReconnectPolicy.h`, testada.
- **Marca:** título "<desktop> - FarSightVNC"; login, opções, ajuda de linha de comando, transferência de arquivos, mensagens, bandeja e "Sobre" dizem FarSightVNC. O "Sobre" diz que é baseado no TightVNC Viewer 2.8.90 e mostra o copyright dos contribuidores do FarSightVNC, **mantém a linha da GlavSoft LLC** e o aviso GPLv2-or-later sem garantia, mais o link do código-fonte. VERSIONINFO: ProductName/FileDescription FarSightVNC; LegalCopyright mantém a GlavSoft. **Decisão a confirmar pelo dono:** CompanyName passou de "GlavSoft LLC." para "FarSightVNC contributors". Chaves de registro, pasta de log, classes de janela e nomes de executável/projeto não mudaram.

## 3. Verificação final (gestor, Rebuild limpo Release x64 e Win32)

- Build OK: `tvnviewer`, `viewer-keyboard-test`, `viewer-input-queue-test`, `viewer-favorites-test` (novo).
- `viewer-keyboard-test` 200/0, `viewer-input-queue-test` 39/0, `viewer-favorites-test` 88/0 (favoritos 33, toolbar 25, reconexão 30) em x64 e Win32.
- `tools/keyboard-e2e-test.ps1` 10/10 e `tools/phase2-test.ps1 -DisplayModes` (4 tamanhos) 200/0, em GDI e em GPU.
- Smoke contra o servidor oficial 2.8.90 em 127.0.0.1 (tight, zrle, hextile, rre, senha errada): OK em x64 e Win32; título "desktop - FarSightVNC".
- Testes de execução dos subagentes (só mensagens para as janelas do próprio viewer, sem SendInput): opções 2 rodadas de gravação/leitura OK; favoritos 21/21; toolbar 30/30 em GDI e 30/30 em GPU; reconexão 21/21 (queda, servidor reiniciado, limite de tentativas, fechar sem reconexão, senha errada sem nova tentativa automática).
- Registro: `HKCU\Software\TightVNC\Viewer` restaurado a cada etapa com SHA-256 igual ao backup (`FarsightVNC-testenv\f4-backup`); histórico do usuário intacto; `HKCU\Software\FarSightVNC` não existe; nenhum processo de teste restante.

## 4. Não verificado

- Ctrl+Tab e Shift+Tab com teclado real; aparência visual dos diálogos (só texto e geometria foram conferidos); DPI acima de 96 e mais de um monitor (máquina com um 1920x1080).
- Botões Ctrl+Alt+Del e Opções da toolbar em execução; contagem de eventos de ponteiro no servidor com o mouse sobre a toolbar.
- Quedas de rede reais e timeouts lentos (só localhost); reconexão contra um servidor TightVNC real (o smoke só cobre a conexão).
- Builds Win32 e Debug só compilados e com testes de unidade; os testes de execução rodaram em Release x64.
- Favorito que sai do histórico pelo limite perde as opções salvas (comportamento herdado do histórico).
