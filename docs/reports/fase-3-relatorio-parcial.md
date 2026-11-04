# FarSightVNC — Relatório parcial da Fase 3 (Renderização e latência)

- **Data:** 2026-11-02
- **Executado por:** Claude Opus 5.5 (gestor) e dois subagentes Claude Sonnet 5.5
- **Estado:** **fase incompleta e interrompida.** Os dois subagentes pararam no limite de uso de 5 horas (HTTP 429). Nada foi integrado ao `develop` e nada desta fase foi enviado ao GitHub.

## 1. Escopo do plano e situação

| Item da Fase 3 | Situação |
|---|---|
| Harness de medição (4K, FPS, latência) | Feito e commitado em `feature/fase-3-harness` |
| Fila de entrada fora da thread de UI | Em andamento: compila, **não commitado e sem teste funcional** |
| `TCP_NODELAY` na conexão do viewer | Em andamento, junto com a fila |
| Redução do tempo de `m_fbLock` | **Não iniciado** |
| Superfície D3D11/D2D com fallback WARP/GDI | **Não iniciado** |
| Avaliação do libjpeg-turbo | **Não iniciada** (só se o profiling justificar) |
| Critério de aceite (melhora medida em 4K com escala) | **Não atendido:** não há build da Fase 3 medido |

## 2. Branches e worktrees

| Branch | Worktree | Estado |
|---|---|---|
| `feature/fase-3-harness` | `D:\DevelopmentLibrary\Apps\FarSightVNC-fase3-harness` | 3 commits sobre `a3640bc`, mais este relatório |
| `feature/fase-3-render` | `D:\DevelopmentLibrary\Apps\FarSightVNC-fase3` | Nenhum commit; alterações pendentes no worktree |

Os worktrees foram criados para isolar a Fase 3 das sessões paralelas. O `develop` avançou depois disso (`8307444`, Fase 1 de teclado); as duas branches ainda não incorporam essas mudanças.

### 2.1 Commits do harness

| Commit | Descrição |
|---|---|
| `1fb13ee` | `tools/fake-rfb-server.py`: servidor RFB falso, somente loopback |
| `753267b` | `tools/render-probe.py`: latência de entrada e de exibição |
| `338d47b` | `tools/measure-render.ps1`: orquestra a comparação entre builds |

### 2.2 Trabalho pendente em `feature/fase-3-render`

Arquivos novos: `viewer-core/InputEventSender.cpp` e `.h`.
Alterados: `RemoteViewerCore.cpp/.h`, `TcpConnection.cpp`, `viewer-core.vcxproj` e `.filters`.

- **Verificado:** build Release|x64 com 0 erros, às 20:53 de 2026-11-01, depois da última edição dessas fontes.
- **Não verificado:** Debug, Win32, avisos novos, ordem FIFO dos eventos, desconexão no meio da sessão e encerramento da thread.
- **Falta:** linhas em `MODIFICATIONS.md`, teste de ordem e de desconexão, e o commit. O subagente parou enquanto corrigia o script de teste descartável, que não foi preservado.

## 3. Medição de validação do harness

Comando: 2 repetições de 15 s, 3840x2160, Raw, tela cheia sem limite de FPS. Dados em `build-logs/render-20261101-205456.*` (ignorados pelo Git).

| Modo | Viewer | CPU (% de 1 núcleo) | FPS | Entrada med/p95 (ms) | Exibição med/p95 (ms) |
|---|---|---|---|---|---|
| ajustar | oficial 2.8.90 | 184,6 | 22,4 | 24 / 47,2 | 77,4 / 100,4 |
| ajustar | compilado | 167,0 | 29,1 | 18,5 / 40,3 | 71,9 / 93,1 |
| 100% | oficial 2.8.90 | 101,6 | 32,3 | 0,1 / 1,4 | 10,9 / 18,3 |
| 100% | compilado | 85,7 | 32,4 | 0,2 / 1,5 | 11,7 / 19,5 |

**Como ler estes números:** o build "compilado" é o do worktree do harness, **sem nenhuma mudança da Fase 3**. As diferenças são ruído, ou efeito do toolset, e não ganho da fase. Havia outras sessões usando a máquina, e duas repetições só provam que o harness funciona. Ele servirá de referência para a próxima medição, que deve rodar com a máquina ociosa.

## 4. Impacto na máquina

- **Processos:** nenhum viewer, Python ou MSBuild de teste ficou rodando (verificado em 2026-11-02).
- **Portas:** nada escutando em 5930–5939.
- **Serviço TightVNC da empresa:** não foi tocado.
- **Registro do Windows:** o script limpa o histórico do viewer, mas essa limpeza **não foi conferida** nesta rodada.
- **Firewall e serviços:** nenhuma alteração feita.

## 5. Como retomar

1. Atualizar `feature/fase-3-render` e `feature/fase-3-harness` com o `develop`, resolvendo eventuais conflitos.
2. Terminar a fila de entrada: testes de ordem e desconexão, as quatro configurações de build, `MODIFICATIONS.md` e commit.
3. Reduzir o tempo de `m_fbLock` em `viewer-core/FbUpdateNotifier.cpp`.
4. Implementar o renderizador D3D11/D2D com fallback.
5. Medir com `tools/measure-render.ps1`, com a máquina ociosa, contra o oficial e o `develop`.
6. Decidir sobre o libjpeg-turbo a partir do profiling.

O uso do Codex nesta fase ficou **sem decisão**: nenhuma das opções apresentadas foi escolhida.
