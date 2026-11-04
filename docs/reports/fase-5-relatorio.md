<!-- Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later. -->
# FarSightVNC — Relatório da Fase 5 (estabilidade e segurança)

- **Data:** 2026-11-03
- **Branch:** `feature/fase-5-estabilidade-seguranca`, rebaseada sobre `develop` `d7ead3d` (Fases 1-3 mescladas). Substitui `fase-5-relatorio-parcial.md`, mantido como histórico.
- **Executado por:** Claude (gestor) e um subagente Claude para o fuzzing.

## 1. Situação por item

| Item | Situação | Evidência |
|---|---|---|
| Guia de túnel SSH/VPN | Feito | `728106c`, `docs/seguranca/tunel-ssh-vpn.md` |
| Avaliação VeNCrypt | Só avaliação; **não implementado** (proposta não aprovada) | `728106c`, `docs/seguranca/avaliacao-vencrypt.md` |
| zlib 1.2.13 -> 1.3.2 | Feito, assinatura e hash conferidos | `63069bd`, seção 2 |
| Fuzzing dos decoders | Feito; 1 defeito real corrigido | `25b67b2`, `1484c80`, `baec78f`, `c4d0f56`, seção 3 |
| Sessão longa | Feito (2 h, GDI e GPU), sem tendência de vazamento | `773d1cf`, seção 4 |

## 2. zlib 1.3.2

- `zlib-1.3.2.tar.gz` baixado de zlib.net e do release do GitHub (`madler/zlib`): arquivos idênticos, SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`, igual ao publicado em zlib.net.
- Assinatura `.asc` válida ("Good signature") da chave de Mark Adler `5ED4 6A67 21D3 6558 7791 E2AA 783F CD8E 58BC AFBA` (DSA, 2001), obtida de dois keyservers num keyring temporário. A chave não tem certificação de confiança na teia local; a identidade foi aceita pela impressão digital conhecida e pela coincidência das duas fontes.
- Os 26 arquivos alterados no stash anterior são idênticos aos do tarball (ignorando CRLF); `crc32.h` e `trees.h` já eram iguais. `zlib.vcxproj` não mudou e não usa `gzclose.c`/`gzread.c`/`gzwrite.c`, novos no pacote.
- Build Release x64/Win32 sem avisos no zlib; smoke contra o servidor oficial 2.8.90 em 127.0.0.1 com tight, zrle, hextile, rre e senha errada: OK nas duas arquiteturas.
- O stash `F5 pendente antes do rebase` foi aplicado e **preservado** (não removido).

## 3. Fuzzing

Ferramentas: `tools/fuzz-rfb-server.py` (servidor RFB falso em 127.0.0.1, só stdlib, mensagens estruturadas malformadas e aleatórias com seed fixa) e `tools/fuzz-test.ps1` (classifica cada caso em OK, HANG ou CRASH e encerra o viewer). Build Debug x64 (/RTC1, heap de depuração da CRT). Encodings: Tight, ZRLE, Hextile, RRE, CopyRect, RichCursor e DesktopSize.

| Campanha | Casos | Resultado |
|---|---|---|
| Antes da correção, 32 bpp | 50 seeds x 7 = 350 | 7 CRASH (rre 5, hextile 1, tight 1), 0 HANG |
| Depois, 32 bpp | 200 seeds x 7 = 1400 | 1400 OK |
| Depois, 8 bpp (`-Use8Bit`) | 100 seeds x 7 = 700 | 700 OK |
| `-Regression` (Debug) | 9 | 9 OK; os 2 cenários manuais falham sem a correção (0xC0000374) |
| `-Regression` (Release x64, gestor) | 9 | 9 OK |

**Defeito corrigido (`1484c80`):** `FrameBuffer::fillRect` (`rfb/FrameBuffer.cpp`) preenchia a primeira linha do retângulo recortado antes de olhar a altura. Um retângulo de altura 0 começando exatamente na borda inferior passa no recorte e escrevia até uma linha além do fim do buffer (fill do Tight, fundo e sub-retângulos do RRE/Hextile). Efeito: corrupção de heap. Correção: retorno antecipado quando o recorte é vazio. Um servidor malicioso conseguia provocar a escrita; o conteúdo escrito é uma cor escolhida pelo servidor.

## 4. Sessão longa

`tools/long-session.ps1`, 119 min, servidor oficial 2.8.90 em 127.0.0.1:5929, um viewer GDI e um GPU simultâneos em view-only, amostra por minuto, aquecimento de 10 min. O viewer mostra a própria área de trabalho que contém as janelas, então o servidor continua enviando atualizações.

| Renderizador | Memória privada (10 min -> fim) | Handles | GDI | USER | Caiu? |
|---|---|---|---|---|---|
| GDI | 27,6 -> 31,5 MB; estável em 31,5 desde o min 50 | 245 -> 245 | 33 -> 33 (33-34) | 28 -> 28 | não |
| GPU | 151 -> 156 MB; pico 169 MB entre min 20-30 | 466 -> 466 (462-468) | 33 -> 33 | 32 -> 33 (31-34) | não |

Conclusão: platô depois do aquecimento, sem crescimento contínuo de memória, handles ou objetos GDI/USER em 2 h. Duas tentativas anteriores foram interrompidas (reinício do Orca, pausa a pedido do dono) e descartadas; os resíduos de registro foram limpos.

## 5. Verificação final (Release x64 e Win32, Rebuild limpo)

- `tvnviewer`, `viewer-keyboard-test`, `viewer-input-queue-test`: build OK nas duas plataformas.
- `viewer-keyboard-test` 200/0 e `viewer-input-queue-test` 39/0 em x64 e Win32.
- `tools/keyboard-e2e-test.ps1` 10/10 em GDI e em GPU.
- `tools/phase2-test.ps1 -DisplayModes` (1920x1080, 1080x1920, 1366x768, 1366x769) 200/0 em GDI e em GPU.
- Sem regressão em relação às Fases 1-3. Nenhum processo de teste restante; `HKCU\Software\TightVNC\Viewer\History` igual ao estado inicial; `HKCU\Software\TightVNC\Server` removida.

## 6. Não verificado

- Fuzzing em Release completo e Win32 (só a regressão rodou em Release x64); sem Application Verifier (exigiria HKLM).
- 16 bpp não é alcançável pelo viewer (só 32 ou 8 bpp); por isso uma suspeita de estouro no ZRLE (`readPalette`/`readSolidTile` com `m_numberFirstByte` = 1) **não foi confirmada nem corrigida**.
- Handshake não foi fuzzado; por leitura, um nome de desktop com comprimento 0xFFFFFFFF em `clientAndServerInit` pode virar buffer vazio. Não confirmado.
- Alocações de 4 GB declaradas não foram testadas (o fuzzer limita tamanhos).
- Sessão longa: não foi confirmado que a tela mudou o tempo todo (bloqueio de tela reduziria as atualizações); sessões acima de 2 h, rede real e servidores de terceiros.
- Guias de túnel SSH/VPN não foram executados; detalhes de VeNCrypt vieram de memória do subagente (ver o relatório parcial).
