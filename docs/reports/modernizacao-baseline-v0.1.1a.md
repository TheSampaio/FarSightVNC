# Baseline reproduzível do v0.1.1a (T1 da modernização)

- Commit base: `9449026` (`fix: FarSightVNC v0.1.1a - ...`), branch `TheSampaio/fsv-t1-baseline`.
- Nenhum código de produto alterado. A única alteração versionada é este relatório e uma linha em `MODIFICATIONS.md`.
- Ambiente: Windows 11 Pro 10.0.26200, VS 18 Community, MSVC v145, Python 3.13.
- Saídas em `build-logs/` (ignorada pelo git) e nas pastas de build (`Debug/`, `Release/`, `x64/`), todas dentro deste worktree.

## 1. Build do tvnviewer (`tools/build.ps1 -Target Rebuild`)

| Configuração | Código de saída | Tempo | Avisos (linhas no log) | Erros |
|---|---|---|---|---|
| Release\|x64 | 0 | 113 s | 70 | 0 |
| Release\|Win32 | 0 | 119 s | 36 | 0 |
| Debug\|x64 | 0 | 93 s | 70 | 0 |
| Debug\|Win32 | 0 | 96 s | 38 | 0 |

Avisos principais (Release x64): C4996 (28, funções "inseguras" em `network`, `util`, `server-config-lib`), C4302/C4311/C4312 (truncamento de ponteiro em `OptionsDialog.cpp`, `InputInjector.cpp`, `Shell.cpp`, `ToolBar.cpp`), C4244/C4267 (conversões), C4091 (`util/CommandLine.h`), LNK4006.

## 2. Testes de unidade (build e execução)

| Teste | Release\|x64 | Debug\|x64 | Release\|Win32 |
|---|---|---|---|
| `viewer-keyboard-test` | 227 OK / 0 falhas (exit 0) | 227 / 0 (exit 0) | 227 / 0 (exit 0) |
| `viewer-input-queue-test` | 39 / 0 (exit 0) | 39 / 0 (exit 0) | 39 / 0 (exit 0) |
| `viewer-favorites-test` | 88 / 0 (exit 0) | 88 / 0 (exit 0) | 88 / 0 (exit 0) |
| `viewer-keysym-test` | não é teste com contagem | exit 0 | exit 0 |

- Todos os builds dos testes saíram com código 0 (tempos de 38 s a 70 s por build).
- `viewer-keysym-test` é o conversor de sequências de teclas para keysyms herdado do TightVNC. Não há aprovação/reprovação; sem argumentos sai com 1 ("Not enough parameters"). Com um arquivo de entrada de 2 linhas, saiu com 0 nas três configurações.
- A contagem de `viewer-keyboard-test` (227) é maior que a do relatório da fase 5 (200), coerente com os casos adicionados em v0.1.1a (Shift/ABNT2).

## 3. Solução inteira (`tightvnc2017.sln`, Rebuild)

| Configuração | Código | Tempo | Projetos | Avisos (MSBuild) | Erros |
|---|---|---|---|---|---|
| Release\|x64 | 0 | 87 s (95 s na restauração) | 47 | 79 | 0 |
| Release\|Win32 | 0 | 89 s | — | 58 | 0 |

## 4. Levantamento C++20 (`/std:c++20`, Release\|x64, sem alterar arquivos versionados)

Método: `MSBuild tightvnc2017.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:ForceImportAfterCppTargets=<arquivo temporário fora do repo com ItemDefinitionGroup/ClCompile/LanguageStandard=stdcpp20>`. Verificado: a flag `/std:c++20` aparece nas linhas de compilação. O arquivo temporário está na pasta de scratchpad, fora do repositório, e não foi commitado.

**Resultado: Build FAILED, 570 ocorrências de erro (454 linhas únicas), 6261 ocorrências de aviso (2074 linhas únicas).** Dos 38 projetos da solução, 24 falharam (17 com erros próprios; os demais por dependência, p.ex. `tvnviewer`, `viewer-keysym-test`). Os testes `viewer-keyboard/input-queue/favorites-test` não aparecem como concluídos no log.

Erros por projeto (linhas únicas, aprox.):

| Projeto | Erros | Projeto | Erros |
|---|---|---|---|
| desktop | 112 | tvncontrol-app | 16 |
| server-config-lib | 86 | win-system | 14 |
| rfb-sconn | 40 | ft-server-lib | 12 |
| viewer-core | 36 | wsconfig-lib | 12 |
| fb-update-sender | 32 | ft-client-lib | 8 |
| rfb | 28 | log-server, util | 6 cada |
| desktop-ipc | 24 | network, io-lib | 2 cada |
| tvnserver-app | 20 | | |

Erros por categoria:

| Código | Ocorrências | Descrição (exemplo) | Projetos principais |
|---|---|---|---|
| C2872 | 136 | `'byte': ambiguous symbol` (`rpcndr.h` vs `std::byte`) | desktop, server-config-lib |
| C2102 | 154 | `'&' requires l-value` | desktop, fb-update-sender, rfb, viewer-core, desktop-ipc |
| C4596 | 54 | `illegal qualified name in member declaration` (p.ex. `WinVideoRegionUpdaterImpl.h`) | tvncontrol-app, tvnserver-app, rfb-sconn, desktop |
| C3878 / C7510 | 40 / 40 | erro de sintaxe e `typename` obrigatório (`ListenerContainer.h`) | ~10 projetos, 4 linhas cada |
| C2065 / C3861 | 8 / 8 | `hextileRaw` não declarado (`HextileTile.h`) | rfb-sconn |
| C2664 / C2440 | 6 / 6 | conversão `const wchar_t[]` → `TCHAR*` (`MirrorDriverClient`); `bool` → `HCURSOR` | desktop, win-system |

Avisos por categoria (linhas únicas no log):

| Código | Ocorrências | Descrição |
|---|---|---|
| C5040 | 1962 (94 arquivos) | `throw()` dinâmico: "valid only in C++14 and earlier" (p.ex. `file-lib/WinFileChannel.h`) |
| C4996 | 60 | funções inseguras (`network`, `server-config-lib`, `util`, `wsconfig-lib`) |
| C5033 | 12 | `register` não suportado (`util/DesCrypt.h`, `tvncontrol-app`, `viewer-core`) |
| C4302/C4311/C4312 | 8/8/2 | truncamento de ponteiro (`desktop`, `win-system`, `gui`) |
| C4018, C4091, C4244, C4267 | 2–6 cada | conversões e declarações |

Busca por itens da lista da migração: `auto_ptr`, `char8_t`, `u8"`, `register` em código, `bad_alloc` e `std::wstring_convert`: sem ocorrências. `register` aparece só como aviso C5033 (classe de armazenamento). `std::byte` aparece em um erro (C2872).

## 5. Fuzzing e medição

**`tools/fuzz-test.ps1` (Debug\|x64, padrão: 7 codificações × seeds 1–50, 350 casos).** Resultado: **350/350 OK; 0 crash, 0 hang, 0 erro**, 275 s, paralelo 3. Todas as codificações: ok=50. Código de saída do script não capturado nesta execução (chamado com `&`); o resumo mostra 0 falhas. As portas ficaram na faixa fixa do script (5933–5945, `ValidateRange`); não havia listener em 5910–5999 antes da execução. Ver "Limitações".

**`tools/measure-baseline.ps1` (Release\|x64, servidor falso em 127.0.0.1:5960, 30 s, 3 repetições, view-only).** O script exige um servidor VNC em execução; usei `tools/fake-rfb-server.ps1` (padrão de teste, sem o servidor oficial, sem a chave `HKCU\...\Server`). Não é o servidor oficial, então os números não são comparáveis aos de outras medições.

| Medida | Mediana | Variação (3 execuções) |
|---|---|---|
| CPU (% de um núcleo) | 17,8 | 17,8 / 17,8 / 20,4 |
| Pico working set (MB) | 42,7 | 42,7 – 43,5 |
| Pico memória privada (MB) | 27,7 | 27,7 – 27,7 |
| Pico de handles | 254 | 254 – 260 |
| Pico GDI / USER | 36 / 30 | 36 / 30 (constantes) |

Código de saída do script não capturado (chamado com `&`); o CSV tem as 3 linhas (`build-logs/baseline.csv`).

## 6. Artefatos, pastas de saída e scripts

Tamanho de `tvnviewer.exe`:

| Build | Bytes | MiB |
|---|---|---|
| `x64\Release` | 1.257.472 | 1,20 |
| `Release` (Win32) | 1.103.872 | 1,05 |
| `x64\Debug` | 5.169.664 | 4,93 |
| `Debug` (Win32) | 4.136.960 | 3,95 |

Pastas de saída (OutDir/IntDir efetivos em `tvnviewer.vcxproj`; o padrão é o mesmo para os demais projetos):

| Plataforma | OutDir | IntDir |
|---|---|---|
| Win32 | `<Config>\` (`Debug\`, `Release\`) | `<Config>\<projeto>\` |
| x64 | `x64\<Config>\` | `x64\<Config>\<projeto>\` |

| Pasta (no worktree) | Arquivos | Tamanho |
|---|---|---|
| `Debug/` (Win32, inclui `Debug\<projeto>\`) | 574 | 133,5 MB |
| `Release/` (Win32) | 1066 | 648,9 MB |
| `x64/` (`x64\Debug`, `x64\Release`) | 1728 | 878,7 MB |
| `build-logs/` | 35 | 9,0 MB |

Não existem `bin/` nem `obj/`. Os testes ficam em `Release\viewer-*-test.exe` (Win32) e `x64\<Config>\viewer-*-test.exe`.

Referências a esses caminhos em `tools/`:

| Script | Linha(s) | Caminho usado |
|---|---|---|
| `build.ps1` | 20, 33, 39 | `build-logs\` (`-LogDir`) |
| `fuzz-test.ps1` | 47 | `x64\Debug\tvnviewer.exe` (padrão) |
| `keyboard-e2e-test.ps1` | 24 | `x64\Release\tvnviewer.exe` (padrão) |
| `keyboard-sendinput-test.ps1` | 22 | `x64\Release\tvnviewer.exe` (padrão) |
| `gpu-presenter-test.ps1` | 7–8 | `x64\Release` (libs), `build-logs\gpu-presenter-test` |
| `long-session.ps1` | 27 | `build-logs\long-session.csv` (+ exemplos `x64\Release`) |
| `measure-baseline.ps1` | 27 | `build-logs\baseline.csv` (+ exemplos `x64\Release`) |
| `measure-render.ps1`, `smoke-test.ps1`, `phase2-test.ps1` | exemplos | `x64\Release\tvnviewer.exe`, `build-logs\` |
| `fake-rfb-server.py`, `render-probe.py` | exemplos | `build-logs/server.json` |

Não há referências a `Win32\`, `bin/` ou `obj/`. Para a migração para `build/bin` e `build/obj`, esses scripts precisam de um padrão novo.

## Limitações e o que NÃO foi verificado

- **Código de saída** do `fuzz-test.ps1` e do `measure-baseline.ps1` não foi capturado (scripts chamados com `&`). Os resumos mostram o resultado.
- **Portas**: `fuzz-test.ps1` só aceita 5933–5945 (`ValidateRange` do próprio script), fora da faixa 5960–5999 pedida para servidores falsos. Não houve conflito: nenhuma porta de 5910 a 5999 estava escutando antes da execução. Não alterei o script.
- **Medição**: servidor falso (`fake-rfb-server.ps1`), não o TightVNC oficial; 3 repetições; sem valores de referência para comparação.
- **C++20**: o levantamento é de compilação. Não tentei corrigir nem diagnosticar a causa de cada erro. Não foi compilado o `Debug`, nem o `Win32` com C++20. `tvnviewer` e os testes não compilaram por causa das dependências, então não há dados de C++20 sobre o viewer.
- **Fuzzing** só em Debug x64 (regressão e Release/Win32 não rodados). `keyboard-e2e-test`, `phase2-test`, `long-session`, `measure-render` e `gpu-presenter-test` não foram executados.
- **Testes de teclado de ponta a ponta** não foram executados nesta tarefa.
