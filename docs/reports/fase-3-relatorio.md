<!-- Copyright (C) 2026 FarSightVNC contributors. GPL-2.0-or-later. -->
# FarSightVNC — Relatório da Fase 3 (renderização e latência)

- **Data:** 2026-11-02
- **Branch:** `feature/fase-3-render` (sobre as Fases 1-2 revisadas, `d63adb3`). Nada foi enviado ao GitHub.
- **Escopo final reduzido** a pedido do dono para economizar tokens: o item B não foi implementado (ver 2).

## 1. Situação por item

| Item | Situação | Evidência |
|---|---|---|
| A. Fila de entrada e `TCP_NODELAY` | Feito | `f33c773`, `9f2dfe2`; `viewer-input-queue-test` 39/0 nas 4 configurações, 30 execuções seguidas sem falha (Release x64) |
| B. Menor tempo de `m_fbLock` | **Não adotado; pendente** | seção 2 |
| C. Direct3D 11 / Direct2D com fallback GDI | Feito, **opcional (GDI é o padrão)** | `3206a23`, `e1370bc`; `gpu-presenter-test` 22/0 |
| D. libjpeg-turbo | Avaliação escrita, sem código | `dc1c85a`, `fase-3-libjpeg-turbo.md` |
| E. Medições | Feito | seção 4 |

## 2. Item B (`m_fbLock`)

O código de divisão em faixas deixado pelo Codex (`FbUpdateNotifier.cpp`) foi revisado, medido e **descartado, sem commit**. Medi o tempo de posse do lock no notificador com instrumentação temporária (não commitada), 3 execuções de 15 s por modo, 4K Raw: a mediana da posse é ~0,024 ms e o p95 ~0,04 ms nas duas versões; as posses acima de 0,3 ms ocorrem ~15 vezes em 15 s e vêm da troca de tamanho do framebuffer (~25 ms, igual antes e depois). O FPS e a latência fim a fim também não diferiram (5 execuções por modo: 35,2 vs 35,1 FPS em "ajustar"). Os decodificadores já notificam em pedaços pequenos, então o lock não é o gargalo neste cenário e a mudança só acrescentaria risco. **Pendente:** reavaliar com servidor real enviando retângulos muito grandes por codificação diferente de Raw (Tight/ZRLE) e com Region fragmentada. Patch descartado preservado em `FarsightVNC-testenv\f3-builds\fbnotifier-bands.patch`.

## 3. Defeitos encontrados no trabalho deixado pela sessão anterior

1. **Fila de entrada:** uma transição de botão (press/release) podia ser fundida a um movimento posterior e perder a posição. Agora só dois movimentos puros consecutivos são fundidos.
2. **Teste TCP escrito para a fila nunca terminava:** no Windows, `shutdown(SD_BOTH)` não interrompe um `recv` já pendente enquanto o par não responde ao FIN. O teste agora representa o par (que fecha ao ver o FIN) e verifica o FIN; o limite fica documentado no teste.
3. O teste usava a porta 5943 (agora 5923), tinha threads destacadas após timeout (agora encerra o processo) e perdia a saída em buffer ao sair pelo watchdog.
4. `ViewerInstance` fazia `shutdown` do socket antes de `stop()`, abortando o envio das liberações de tecla no modo escuta.
5. **GPU:** a primeira apresentação criava o dispositivo D3D na thread da janela (~190 ms) e, em modo GPU, o teste de teclado de ponta a ponta perdia as primeiras teclas (8 de 8 execuções). A criação passou a ser em thread de trabalho (GDI pinta até ficar pronto); depois disso 8 de 8 passam.
6. Ordem de CRLF/LF dos arquivos foi preservada nos commits (vários arquivos do repositório têm fins de linha mistos).

## 4. Medições (item E)

Harness `tools/measure-render.ps1` (servidor falso Raw, 3840x2160, sem limite de FPS), 5 repetições de 15 s (aquecimento de 3 s), intercaladas, **medianas**. Binário oficial `2.8.90` vs. build novo com `-renderer=gdi` e `-renderer=gpu`. Arquivos brutos: `FarsightVNC-testenv\f3-results\E\render-20261102-134656.*`.

| Modo | Viewer | CPU % (1 núcleo) | WS MB | Privado MB | Handles | FPS | Entrada med/p95/máx (ms) | Exibição med/p95 (ms) |
|---|---|---|---|---|---|---|---|---|
| ajustar | oficial | 191,1 | 112,5 | 98,7 | 246 | 37,7 | 16,5 / 31,6 / 35,3 | 59,5 / 74,6 |
| ajustar | novo GDI | 173,8 | 112,5 | 98,7 | 250 | 37,5 | 16,5 / 31,4 / 37,0 | 58,6 / 73,5 |
| ajustar | novo GPU | 191,1 | 237,9 | 341,1 | 471 | 31,6 | 0,3 / 1,5 / 8,5 | 11,1 / 17,0 |
| 100% | oficial | 101,1 | 112,6 | 98,7 | 246 | 38,5 | 0,1 / 1,4 / 2,7 | 11,4 / 18,2 |
| 100% | novo GDI | 92,4 | 112,6 | 98,7 | 250 | 39,6 | 0,2 / 1,5 / 2,7 | 9,8 / 18,5 |
| 100% | novo GPU | 129,2 | 265,2 | 362,7 | 468 | 35,1 | 0,2 / 3,8 / 6,6 | 19,0 / 27,1 |

Leitura honesta:

- **GPU em "ajustar" 4K:** a latência de entrada cai de ~16 ms para ~0,3 ms (a thread da janela deixa de fazer `StretchBlt`) e a de exibição de ~59 ms para ~11 ms. O custo: CPU igual, FPS menor (31,6 vs 37,5), e memória 2-3,5x maior (working set 238 MB, privado 341 MB).
- **GPU a 100%:** pior que GDI em CPU (129 vs 92 %), exibição (19 vs 10 ms), FPS e memória.
- **Novo GDI vs. oficial:** diferenças pequenas (CPU 174 vs 191 % em "ajustar"); com a máquina compartilhada e 5 repetições não as atribuo a ganho real, apenas "sem regressão".
- Havia outras sessões usando a máquina (carga base ~10 %); o `máx` de entrada é ruído de cauda. A medição usa PostMessage na janela do próprio viewer e servidor Raw sintético: não representa Tight/ZRLE nem rede real, nem GPU integrada de outras máquinas (testado só na GPU desta máquina).

## 5. Renderizador: como escolher (documentação)

Padrão: **GDI** (`RenderSettings::DEFAULT_MODE`). A GPU é opt-in porque o ganho só existe ao reduzir/ajustar desktops grandes e o custo de memória e o pior desempenho a 100% impedem torná-la padrão.

| Fonte (da mais forte à mais fraca) | Uso |
|---|---|
| Linha de comando | `-renderer=gdi` \| `auto` \| `gpu` |
| Variável de ambiente | `FARSIGHTVNC_RENDERER=gdi\|auto\|gpu` |
| Registro (HKCU) | `Software\TightVNC\Viewer\Settings`, valor texto `Renderer` |

`auto` e `gpu` usam Direct3D 11/Direct2D (só bibliotecas do Windows SDK, carregadas em tempo de execução) e **caem para GDI automaticamente** em: dispositivo perdido ou removido, formato de pixel não suportado (8/16 bits), quadro maior que o limite do dispositivo, adaptador só-software (WARP), bibliotecas ausentes; após 3 falhas seguidas a GPU é desligada na sessão. A cadeia de troca é do modelo bit-blt de propósito: o modelo flip impede definitivamente a pintura GDI na janela e destruiria o fallback. `-renderer=gdi` força GDI e nem cria o apresentador.

## 6. Verificação final

- Rebuild limpo (`tools/build.ps1 -Target Rebuild`) de `tvnviewer`, `viewer-keyboard-test` e `viewer-input-queue-test`: ver o resumo no commit final; testes: teclado 200/0, fila 39/0.
- `tools/phase2-test.ps1 -DisplayModes` (Release x64, 4 tamanhos x 100/50/auto, porta 5923): **200 PASS / 0 FAIL** em GDI e em GPU.
- `tools/keyboard-e2e-test.ps1`: 10/10 em GDI e em GPU. Observado uma vez, em 24 execuções GDI, uma execução com 3 casos e 2 falhas (não reproduzida em 8 repetições seguintes; causa não investigada).
- `tools/gpu-presenter-test.ps1`: 22 PASS / 0 FAIL na GPU desta máquina.

## 7. Não verificado

- Perda real de dispositivo (TDR) da GPU: o caminho `fail()` -> GDI -> recriação foi revisado por leitura, não provocado.
- DPI 125/150/200 % e troca de monitor (a máquina roda a 96 DPI, como na Fase 2); GPU integrada vs. discreta; sessões RDP.
- Tight/ZRLE e redes reais; ganho do libjpeg-turbo (depende de perfil com Tight/JPEG).
- Configurações Debug e Win32 do viewer foram compiladas, mas os testes de aceitação e as medições rodaram em Release x64.
- Item B pendente (seção 2).

## 8. Cópia de segurança e commits

Trabalho pendente do Codex: `D:\DevelopmentLibrary\Apps\FarsightVNC-testenv\f3-codex-pending.patch` e `f3-codex-untracked\`.
Commits (do mais antigo): `f33c773`, `9f2dfe2`, `267f4ba`, `3206a23`, `dc1c85a`, `e1370bc` sobre `11e33b9`.
