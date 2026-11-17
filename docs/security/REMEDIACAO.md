<!-- Modified for FarSightVNC on 2026-11-17: remediation matrix for the 2026-11-13 security audit. -->

# Remediação da auditoria de segurança do FarSightVNC

Base: `docs/security/FARSIGHT_SECURITY_AUDIT.md`, de 2026-11-13, feita sobre `develop` no commit
`50557e9`, versão `v1.1.0a`.
Remediação iniciada em 2026-11-17, sobre `develop` no commit `6538cec`, versão `v1.3.0a`.

Este documento é a matriz de remediação: por achado, a severidade, se ainda se aplica ao código
atual, a causa-raiz, a correção, o teste e o estado. Ele não substitui a auditoria e não repete a
análise dela.

---

## 1. Verificação de aplicabilidade: auditoria `50557e9` → HEAD `6538cec`

A auditoria foi feita há quatro dias e nove commits. Antes de corrigir qualquer coisa, cada arquivo
citado pela auditoria foi comparado entre o commit auditado e o HEAD atual, com
`git diff --numstat 50557e9 HEAD -- <arquivo>`.

| Área | Estado desde a auditoria | Consequência |
|---|---|---|
| `ft-client-lib`, `ft-common`, `file-lib`, `io-lib`, `util`, `network` | **Idênticos, byte a byte** (`git diff --stat` vazio) | Todos os achados D-*, A-05/A-06/A-07 e R7 continuam **integralmente aplicáveis** |
| `rfb/FrameBuffer.cpp` | Inalterado | A-04, e as mitigações B-02/B-03, continuam como auditadas |
| `viewer-core/ZrleDecoder.cpp`, `RreDecoder.cpp`, `DecoderStore.cpp`, `TightDecoder.cpp` | Inalterados | B-01 (mitigado), B-04, B-07, A-09, B-10 continuam como auditados |
| `viewer-core/RemoteViewerCore.cpp` | Inalterado | A-02, A-07, A-08, A-10, C-04, A-15, M-1 continuam como auditados |
| `win-system/DibSection.cpp` | Inalterado | C-04 continua aplicável |
| `tvnviewer/ConnectionListener.cpp`, `ConnectionData.cpp`, `client-config-lib/ConnectionConfig.cpp` | Inalterados | C-03, A-06, C-02/C-05 continuam aplicáveis |
| `win-system/WinHooks.cpp` | **271 linhas adicionadas, 10 removidas** | C-06 reavaliado: **continua aplicável**, ver abaixo |
| `tvnviewer/DesktopWindow.cpp` | **130 linhas adicionadas, 4 removidas** | C-02/C-05 precisam de reavaliação na área de clipboard |
| `tvnviewer/UpdateController.cpp`, `updater-lib/*` | Alterados pela v1.3.0a | E-01/E-02/E-03 precisam de reavaliação |

### Reavaliação de C-06 (tecla Windows engolida pelo hook)

O trabalho de teclado da v1.3.0a reescreveu `win-system/WinHooks.cpp`, então o achado foi reavaliado
no código atual em vez de presumido. Veredito: **continua aplicável**.

`WinHooks::decideKeyAction` (`win-system/WinHooks.cpp:112-120`) devolve ao Windows apenas eventos
injetados (`LLKHF_INJECTED`) e as teclas `VK_CAPITAL`, `VK_NUMLOCK` e `VK_SCROLL`. Todo o resto,
`VK_LWIN` e `VK_RWIN` incluídos, vira `HOOK_KEY_POST`: é repostado para a janela da sessão e o evento
original é consumido. Com a sessão em foco e o teclado capturado, `Win+L` vai para o servidor remoto e
a estação local **não** bloqueia.

O que a v1.3.0a acrescentou foi um **interruptor de liberação de teclado por sessão**
(`DesktopWindow::m_keyboardReleased`, exposto nas duas barras com indicador de estado) e
`WinHooks::viewerOwnsKeyboard`, que faz o hook não possuir nada enquanto a liberação durar. Isso é
mitigação operacional, não correção: a liberação é um gesto deliberado do operador e, conforme o
relatório de continuidade de 2026-11-16, uma sessão nova começa capturando o teclado. O padrão
continua engolindo a tecla Windows.

C-06 está fora do escopo desta rodada (não é Prioridade 0 nem 1) e consta na seção 5 como risco
remanescente.

### Premissa da auditoria que deixou de valer

A auditoria registrou, na seção 2, que o código de servidor não chega ao binário entregue **por causa
do procedimento de release**, não porque o build o exclua, e classificou isso como F-05, uma garantia
estritamente mais fraca.

Em 2026-11-17 o dono autorizou um servidor FarSightVNC com identidade própria (commits `6284c05`,
`ce3da69`, `e10c3fc`, `6538cec`). **Isso materializa exatamente o risco de F-05**: milhares de linhas
de código de servidor que nenhum dos sete relatórios auditou passam a ser um artefato distribuível.
A auditoria de 2026-11-13 cobre o viewer e o atualizador; ela **não cobre** `tvnserver`, `rfb-sconn`,
`desktop`, `ft-server-lib` nem `http-server-lib`. Consta aqui como risco remanescente, não como
achado corrigido.

---

## 2. Escopo desta rodada

Por decisão do dono, esta rodada executa **Prioridade 0 e Prioridade 1**. As frentes de clipboard,
autenticação, conexão reversa, atualizador e endurecimento de build ficam para uma rodada seguinte e
estão listadas na seção 5 como risco remanescente, não como corrigidas.

---

## 3. Matriz de remediação

Estado: **corrigido** (código alterado e teste determinístico passando), **parcial** (parte da
correção entregue, com o restante declarado), **fora de escopo** (desta rodada), **não aplicável**.

### Fundação

| Achado | Severidade | Aplicável | Causa-raiz | Correção | Teste | Estado |
|---|---|---|---|---|---|---|
| — | — | — | A regra de "este nome é seguro para o sistema de arquivos" existia só como função estática em namespace anônimo em `rfb/ClipboardFilesMessage.cpp:65`, inacessível ao cliente de transferência de arquivos, e sem os nomes de dispositivo reservados do DOS | `util/WindowsFileName.{h,cpp}`: `isSafeNameComponent`, `isReservedDosDeviceName`, `canonicalize` (sem buffer `MAX_PATH`) e `isPathInsideDirectory` (canonicaliza as duas pontas, exige fronteira de separador, compara sem diferenciar maiúsculas, falha fechado). `rfb/ClipboardFilesMessage.cpp` passou a usar a regra compartilhada | `viewer-security-test/FileNameRulesTest.cpp`, 229 checagens, só strings | **corrigido** (`3ccafb6`, `d713fa5`) |

### Prioridade 0 — transferência de arquivos

| Achado | Severidade | Aplicável | Causa-raiz | Correção | Teste | Estado |
|---|---|---|---|---|---|---|
| D-01 / D-03 / D-06 | **Crítica** | Sim, código idêntico | O nome de cada entrada de `FILE_LIST_REPLY` vem do servidor e chega ao caminho local sem filtro nem canonicalização, e o diálogo de confirmação só aparece quando o alvo já existe | `FileInfo::setRemoteFileName` valida no parse e a entrada rejeitada é pulada, registrada e contada; `getLocalPath` devolve `bool` e exige contenção na pasta escolhida, com os dois chamadores barrando; `File::truncate()` não apaga mais para esvaziar, recusa diretório e reparse point e reconfere os atributos no handle aberto; *Skip* é o botão padrão do diálogo | `FileTransferPathTest.cpp` | **corrigido** (`97bbe9e`, `2a4fd86`), com uma exceção: ver *Overwrite All* abaixo |
| D-02 | Alta | Sim, código idêntico | `uncompressedSize` do fio dimensiona a gravação sobre um buffer alocado com `compressedSize` | Tamanho real do buffer como única fonte da verdade; os dois campos divergentes sem compressão é erro de protocolo. Também virou checagem real o `_ASSERT` do tamanho inflado, que no build entregue não verificava nada | `FileTransferWireTest.cpp` | **corrigido** (`2a4fd86`) |
| D-04 | Média | Sim, código idêntico | Despacho de mensagens de FT dirigido só pelo código da mensagem, sem estado; `m_fos->close()` sobre nulo | `DownloadState` explícito e `acceptsReply` consultado no despacho, mais checagem de nulo. O corpo da mensagem continua sendo lido, então a recusa não dessincroniza a sessão | `FileTransferWireTest.cpp` | **corrigido** (`2a4fd86`) |
| D-05 | Média | Sim, código idêntico | Contagem de arquivos u32 do fio dimensiona `new FileInfo[]` antes de qualquer validação | Teto nomeado de 1.000.000 e, mais apertado, limite pelos bytes que realmente chegaram (`(bytes - 4) / 22`), com os dois consumidores limitando de novo | `FileTransferWireTest.cpp` | **corrigido** (`2a4fd86`) |
| D-07 | Média | Sim, código idêntico | Alvo de gravação segue reparse points | Reparse point existente recusado no alvo de arquivo e de pasta; `WinFile`/`WinFileChannel` ganharam modo `refuseReparsePoint` com `FILE_FLAG_OPEN_REPARSE_POINT` e reconferência no handle, usado pelo handle por onde o download grava | `FileTransferPathTest.cpp` | **corrigido** (`805058e`, `2a4fd86`), **recusa não exercitada** — ver seção 4 |
| D-08 | Média (amplificador) | Sim, código idêntico | Quem habilita transferência de arquivos é o servidor; o viewer não tem interruptor | Política de três estados por conexão (Ask, padrão; Allow; Never). Em Never, `addCapabilities` não anuncia nada. Em Ask, confirmação única por sessão, só por ação do operador | sem teste automatizado | **corrigido** (`248a52e`), não exercitado em tempo de execução |

### Prioridade 1 — validação do fio, formato de pixel e RAII

| Achado | Severidade | Aplicável | Causa-raiz | Correção | Teste | Estado |
|---|---|---|---|---|---|---|
| R3: A-07, A-08, A-10, B-04, B-07 | Baixa a Média-baixa cada, **bloqueador como política** | Sim, código idêntico | Campo u32 do fio dimensiona alocação ou laço antes de qualquer validação; em B-04 o teto `maximalUnpackedSize` já existe e é ignorado; em A-08 o teste `length > SIZE_MAX - 1` é inócuo em 64 bits | `viewer-core/RfbLimits.h` enuncia a política e reúne todos os tetos, cada um com o comentário do que o tráfego real produz: razão de autenticação 64 KiB, clipboard 64 MiB lido em blocos, contadores Tight 256, ZRLE amarrado ao que `Inflater::inflate` já aceita, RRE limitado à área do retângulo | `RfbLimitsTest.cpp` | **corrigido** (`1de5e79`) |
| C-04 | Média | Sim, código idêntico | `redMax`/`greenMax`/`blueMax` e os `*Shift` do `ServerInit` não são validados; o ramo de 8 bpp de `DibSection::setupBMIStruct` divide por eles | Formato recusado onde o viewer o adota (`setFbProperties`), mais guarda defensiva em `DibSection::openDIBSection` antes de tomar qualquer recurso | `RfbLimitsTest.cpp`, `FrameBufferRaiiTest.cpp` | **corrigido** (`1de5e79`, `5ba41ee`) |
| A-04 | Média | Sim, código idêntico | `resizeBuffer()` não zera `m_buffer` entre `delete[]` e `new`; o teto de geometria permite ~2 GiB por buffer | Ponteiro zerado entre as duas operações, dimensão limpa e exceção repropagada na falha, e `delete[]` sobre `void *` corrigido com cast. Teto de geometria em 16384 por lado, com `static_assert` | `FrameBufferRaiiTest.cpp`, `RfbLimitsTest.cpp` | **corrigido** (`1de5e79`, `5ba41ee`) |
| A-09 | Baixa | Sim, código idêntico | `delete[]` sobre objeto criado com `new` escalar em `DecoderStore::addDecoder` | Forma escalar correta, e `removeDecoder` apaga a entrada do mapa antes de liberar | `FrameBufferRaiiTest.cpp` | **corrigido** (`5ba41ee`) |
| B-10 | Baixa | Sim, código idêntico | `delete m_inflater[i]; m_inflater[i] = new Inflater;` sem proteção | `m_inflater` virou `vector<unique_ptr<Inflater>>`: o novo é construído antes de o velho ser liberado | `FrameBufferRaiiTest.cpp` | **corrigido** (`5ba41ee`) |
| B-11 | Informativa | **Não** | Vazamento em `WatermarksController` | Nenhuma: a auditoria confirmou que o arquivo inteiro está sob `#ifdef _DEMO_VERSION_`, que nenhum build define | — | **não aplicável** (código morto) |
| B-01, B-02, B-03 | Já mitigados pelo fork | Sim, mitigações presentes | Corrupção de heap no ZRLE CPIXEL; `fillRect` gravando uma linha além do buffer; `setColor` em laço infinito | Nenhuma correção nova: são **regressões a vigiar**. Testes de regressão explícitos foram adicionados porque são o que uma edição vizinha quebra por acidente. `setColor` com tamanho de pixel zero roda em thread com timeout de 5 s, para que a regressão falhe em vez de travar a suíte | `FrameBufferRaiiTest.cpp` | **vigiado por teste** (`5ba41ee`) |

### Testes efetivamente executados

Rodados pelo coordenador na árvore final, não apenas relatados pelos executores:

| Verificação | Resultado |
|---|---|
| Build CMake completo, x64 Release, todos os alvos | exit 0 |
| `ctest --preset x64-release` | **8/8, zero falhas**, 25,02 s |
| Build oficial MSBuild: viewer, updater e seis testes | exit 0, "All requested configurations built successfully" |
| `viewer-security-test.exe`, build oficial e build CMake | **418 passaram, 0 falharam** nos dois |
| `viewer-clipboard-files-test.exe` (canário de regressão) | 171 passaram, 0 falharam |
| `viewer-keyboard-test.exe` (canário) | 548 passaram, 0 falharam |
| `viewer-input-queue-test.exe` (canário) | 39 passaram, 0 falharam |
| `viewer-favorites-test.exe` (canário) | 146 passaram, 0 falharam |

O total de 418 depende do ambiente: dois dos checks só rodam onde é possível plantar um link
simbólico de arquivo, e nesta máquina eles imprimem `SKIP` e não contam. Numa máquina com Modo de
Desenvolvedor o total é maior. Um binário de teste não reconstruído depois da última mudança informa
396; conferir a data do executável antes de comparar números.

#### Os testes falham de verdade contra o código não corrigido

Isso foi exercitado, não presumido. Desativando na mão a recusa de tamanhos divergentes e o teto de
bloco em `FileTransferReplyBuffer.cpp` e voltando `m_downloadBufferSize = uncoBufferSize`, a suíte
acusou 5 falhas (`391 passed, 5 failed`, exit 1), todas de D-02; o arquivo foi restaurado em seguida.
Desativando as checagens de ZRLE, RRE e `readUTF8`, 9 checagens falharam. Removendo a correção do
`m_buffer`, o processo morreu antes de imprimir o resumo, que é exatamente o double free no destrutor;
removendo o early-return de `fillRect` e a guarda de `setColor`, 10 checagens falharam.

Os demais achados **não** foram demonstrados assim: D-01, D-04 e D-07 usam APIs que não existem no
código não corrigido (`setRemoteFileName`, `acceptsReply`, `isReparsePoint`), então o teste não
compila contra ele.

#### Sanitizador e Application Verifier

AddressSanitizer do MSVC (`/fsanitize=address`) foi compilado e executado num diretório de build
descartável, sem alterar a configuração do projeto. Os dois runners de transferência de arquivos
executaram até o fim **sem nenhum relatório do AddressSanitizer**, que é a evidência pedida para D-02.
O processo depois abortou dentro de `FrameBufferRaiiTest.cpp`, que pede deliberadamente
`0xffffffff80000000` bytes e o ASan trata como `allocation-size-too-big`, então não há tally final sob
ASan — apenas a confirmação de que a área de transferência de arquivos está limpa.

**Application Verifier não foi executado.** O `appverif.exe` existe na máquina, mas habilitá-lo exige
elevação que esta sessão não tem. Não há resultado de Application Verifier para nenhuma correção.

#### O que não foi exercitado

- **A metade de recusa de D-07.** Plantar um link simbólico de arquivo exige Modo de Desenvolvedor ou
  `SeCreateSymbolicLinkPrivilege`, que esta máquina não concede, então o teste imprime `SKIP` e não
  conta nada em vez de simular aprovação. O que **foi** verificado é a metade de compatibilidade: um
  arquivo comum continua abrindo com o modo ativado, que era o risco de regressão da mudança. A recusa
  de junção de diretório, essa sim, foi exercitada com `FSCTL_SET_REPARSE_POINT`, que não exige
  privilégio.
- **Os três estados de D-08**, que não foram exercitados em tempo de execução, nem a tela de
  configuração.
- **`FileExistDialog`**: a troca do botão padrão é diálogo modal do Win32 e não há harness para isso
  no repositório. Verificada apenas por leitura.
- **Nenhuma conexão real**, a nenhum servidor TightVNC nem ao computador do amigo.

### Revisão independente

Um revisor independente releu o diff inteiro `6538cec..HEAD` com a instrução de **atacar** o trabalho,
não de confirmá-lo. Ele percorreu o nome do servidor desde `readUTF8` até cada criação, abertura,
truncagem e exclusão local e não achou nome que passe a validação e ainda saia da pasta, nem caminho
local montado em outro lugar, nem `setFileName` alcançável por nome remoto. Verificou que a validação
roda **depois** da conversão UTF-8, que é a ordem certa: um `\` em UTF-8 overlongo ou um best-fit de
`U+FF3C` já virou `\` quando a checagem acontece. Veredito dele sobre o achado crítico: fechado.

Mas ele encontrou três defeitos reais, todos corrigidos:

1. **Dessincronização da sessão, Média — o mais grave, e era pior que o bug corrigido.**
   `IOException` deriva de `Exception`, então o `catch` do despacho de capacidades em
   `RemoteViewerCore.cpp` engolia **todas** as recusas novas de transferência de arquivos e o laço
   seguia. Como a recusa acontece antes de o resto da mensagem ser lido, o
   `receiveServerMessageType()` seguinte passava a ler o tipo de dentro do corpo da mensagem recusada
   — o atacante escolhia o enquadramento do fluxo. Isso também contradizia a política escrita em
   `RfbLimits.h`, que diz que valor acima do teto encerra a sessão. Corrigido em `a5494b7`:
   `IOException` é relançada e chega ao handler externo, que reporta a desconexão.
2. **`File::isReparsePoint()` falhava aberto, Baixa.** Usava `FindFirstFile` no diretório pai e
   devolvia `false` quando a enumeração falhava, o que é a direção perigosa. No caminho de **pasta**
   essa checagem é a única barreira, então uma junção não detectada levaria a subárvore inteira para
   fora da pasta escolhida com a contenção textual intacta. Era o argumento mais forte do próprio
   revisor contra a traversal estar fechada. Corrigido em `69e370c`.
3. **Recursão sem limite no passeio do download, Baixa/Média.** `gotoNext()` e `startDownload()` se
   chamam, e os caminhos de falha síncrona pedem a entrada seguinte sem esperar o servidor, então a
   pilha não desenrola entre irmãos. O servidor escolhe quantas entradas a listagem tem e quantas
   falham, logo escolheria a profundidade; o teto de um milhão de entradas de D-05 limitava a contagem
   sem limitar a recursão que ela dirige, e as duas recusas novas acrescentaram gatilhos. Corrigido em
   `7b6bc0f` com um trampolim.

Duas imprecisões deste documento que ele achou estão corrigidas no texto acima: a frase sobre C-04 e
servidores de paleta, e a justificativa incompleta do truncamento de `getBufferSize`.

#### Recomendação de D-01 que **não** foi implementada

A auditoria tem três recomendações para D-01 (`FARSIGHT_SECURITY_AUDIT.md`, seção 5, item 10). As duas
primeiras estão feitas. A terceira, **limitar *Overwrite All* ao lote selecionado pelo operador**, não
está: `FileExistDialog::m_overwriteAll` continua valendo para a operação inteira. O caso que importa é
o operador clicar *Overwrite All* vendo um arquivo e aquilo passar a valer para os filhos que o
servidor devolveu numa listagem de pasta, que ele nunca viu. Com a contenção em vigor o dano fica
dentro da pasta escolhida, e o padrão *Skip* reduz o acionamento acidental, mas *Overwrite All* anula
esse padrão. Fica como risco remanescente, não como corrigido.

#### `File::truncate()` também mudou o servidor

`truncate()` é usado pelo caminho de upload do servidor (`ft-server-lib/FileTransferRequestHandler.cpp`
linha 547). A reescrita recusa diretório e reparse point e abre com `shareMode 0`, então o servidor
FarSightVNC — autorizado em 2026-11-17 e distribuível — teve comportamento alterado por esta rodada,
sem auditoria. O revisor não achou quebra concreta (upload para arquivo comum segue funcionando, e o
`remove()` antigo também falhava em arquivo somente-leitura), mas a declaração de compatibilidade da
seção 4 fala só do viewer e está incompleta nesse ponto.

### Achados laterais encontrados durante a remediação

Dois problemas que a auditoria de 13/11 não lista, encontrados ao corrigir o código:

- **`FrameBuffer::getBufferSize()` trunca para `int`** (`rfb/FrameBuffer.cpp:564`): o cálculo é feito em
  `UINT64` e depois convertido, então uma geometria grande o bastante produziria um tamanho pequeno e
  uma alocação menor que o buffer que o resto do código acha que tem — estouro de heap em potencial.
  **Já está defendido na produção**, por dois lados independentes: `bitsPerPixel` só é aceito em
  {8, 16, 32} (`viewer-core/RemoteViewerCore.cpp:851`) e `RfbLimits::fitsDesktopGeometry` limita cada
  lado a 16384, com um `static_assert` que garante em tempo de compilação que 16384 × 16384 × 4 cabe em
  `INT_MAX`. O pior caso alcançável é exatamente 1 GiB. Não foi alterado: a conversão continua frágil
  se algum dia o teto de geometria subir, e o `static_assert` é justamente o que quebra o build nesse
  caso. A revisão independente completou a justificativa: existe um terceiro caminho que dimensiona um
  `FrameBuffer`, o `viewer-core/CursorPainter.cpp:64-65` com a geometria de `RICH_CURSOR` vinda do fio,
  e ele **não** passa por `fitsDesktopGeometry`. Quem o limita é
  `ViewerBufferSize::fitsDecoderBuffer(w, h, 4)` em `RemoteViewerCore.cpp:1167`, que garante área
  ≤ `INT_MAX / 4`. A conclusão se mantém, por dois mecanismos diferentes.
- **`DibSection::releaseTargetDC` chama `ReleaseDC(0, dc)`** para um DC obtido com `GetDC(hwnd)`, o que
  parece vazamento de DC. Não é falha de segurança e está fora do escopo desta rodada.

---

## 4. Compatibilidade com a infraestrutura TightVNC

Regra do projeto: o viewer tem de continuar funcionando contra os servidores TightVNC já instalados,
sem mudança nenhuma do outro lado. Avaliação de cada correção desta rodada:

- **Nenhuma correção desta rodada altera o formato do fio, os códigos de mensagem, os tipos de
  segurança, as codificações ou o handshake.** Não há extensão de protocolo nova aqui.
- As validações de nome e de caminho de D-01 são satisfeitas por qualquer servidor TightVNC legítimo:
  o protocolo transporta apenas *nome de entrada*, nunca caminho, e o servidor TightVNC já filtra
  `.` e `..` na própria enumeração.
- A igualdade exigida em D-02 entre `compressedSize` e `uncompressedSize` quando não há compressão é
  o que um servidor TightVNC correto já envia.
- Os tetos de R3 e o teto de geometria de A-04 ficam acima de qualquer valor que tráfego real produza.
  Um teto apertado demais quebraria conexões legítimas, e esse é o risco a vigiar nesta frente.
- As correções de RAII (A-04, A-09, B-10) são internas.
- **C-04 merece uma ressalva que a primeira versão deste documento errou.** A recusa de formato com
  máximo zero não afeta servidor TightVNC, que sempre anuncia true colour — a regra do projeto está
  preservada. Mas é falso dizer que ela "recusa apenas o que nenhum servidor real anuncia": um Xvnc ou
  RealVNC em 8 bits com paleta anuncia exatamente `redMax = greenMax = blueMax = 0`. Antes, o viewer
  conectava a esses servidores com cores erradas (`readPixelFormat` registrava erro e seguia, e o
  handler de `SetColourMapEntries` existia mas descartava os dados); agora a conexão falha em
  `ServerInit` com mensagem clara. Trocar cores erradas por falha limpa é melhora, e modo paleta nunca
  foi suportado de verdade, mas é mudança de comportamento com servidor não-TightVNC.
- O teto de geometria de 16384 por lado está acima de qualquer desktop plausível no parque do dono,
  mas não é "acima de qualquer valor que tráfego real produza": cinco monitores 4K em linha dão
  19200 px e seriam recusados.
- O padrão **Ask** de D-08 muda **comportamento visível**: transferência de arquivos, e também a
  colagem de arquivos por Ctrl+V, passam a exigir uma confirmação na primeira vez de cada sessão. Não
  muda o protocolo — não anunciar a capacidade é mecanismo previsto — mas precisa constar na nota de
  versão. Entradas novas de configuração ausentes não são erro de carga, e o TightVNC Viewer original
  ignora entradas que não conhece, então `.vnc` e perfis de registro seguem intercambiáveis.

Três mudanças de comportamento que valem aviso na nota de versão, nenhuma de protocolo:

1. Um alvo de download que seja um diretório existente agora falha com erro claro, em vez de o
   diretório ser apagado e substituído por um arquivo.
2. Um alvo de pasta que seja uma junção é recusado, porque `GetFullPathName` não vê através dela e a
   subárvore inteira cairia fora da pasta escolhida.
3. Um servidor que envie um nome que um servidor TightVNC real nunca envia (ponto ou espaço final,
   nome de dispositivo reservado) tem a entrada descartada. O Windows resolve esses nomes para outro
   arquivo, então aceitá-los nunca foi correto, mas um servidor não-TightVNC com nomenclatura solta
   veria entradas desaparecerem.

---

## 5. Risco remanescente

Declarado explicitamente, porque não foi corrigido nesta rodada:

1. **Servidor FarSightVNC não auditado.** A autorização de 2026-11-17 torna distribuível código de
   servidor que a auditoria de 2026-11-13 não cobriu. É o maior item aberto.
2. **Sem criptografia nem integridade no transporte (A-01).** Limitação estrutural do RFB 3.8 com
   servidores TightVNC, que não oferecem VeNCrypt. Mitigação real só fora do viewer: túnel SSH, VPN ou
   o gateway TLS já previsto no roadmap. Não há TLS só-no-viewer que preserve a interoperabilidade com
   o parque instalado.
3. **Clipboard automático nas duas direções (C-02/C-05)**, ligado por padrão. Fora de escopo desta
   rodada.
4. **`None` aceito sem aviso (A-02)** e senha exposta ao desafio do servidor (A-03).
5. **Modo de escuta em `0.0.0.0:5500` sem autenticação nem confirmação (C-03).**
6. **Atualizador (E-01/E-02/E-03)**: integridade do `fsvncupdater.exe` não verificada antes de
   executar ou elevar. A auditoria condicionava a severidade à inexistência de instalação em diretório
   protegido; o instalador da v1.3.0a instala em `%LOCALAPPDATA%\Programs\FarSightVNC`, que continua
   gravável pelo usuário, então a condição ainda não mudou — mas isso precisa ser reavaliado, não
   presumido.
7. **Endurecimento de build (F-01, F-03, F-04, F-05, F-07)**: sem `/guard:cf`, sem defesa
   anti-DLL-planting no viewer, e dois sistemas de build que já divergiram.
8. **Senha salva com DES de chave fixa pública (A-05/A-06/C-09)** e sem Authenticode nos executáveis
   (F-02).
9. ***Overwrite All* não limitado ao lote do operador**, a terceira recomendação de D-01. Ver a seção
   da revisão independente.
10. **A contenção não protege o que está dentro da pasta escolhida.** Se o operador baixar para a
    própria pasta de instalação, um servidor hostil ainda pode trocar o `fsvncviewer.exe`; o que se
    interpõe é só o diálogo com *Skip* padrão, e *Overwrite All* o anula. Isso já consta no impacto de
    D-01 na auditoria.
11. **`FILE_FLAG_OPEN_REPARSE_POINT` cobre só o componente final do caminho.** Um processo local do
    mesmo usuário que troque um diretório intermediário por junção depois do `mkdir` redireciona as
    gravações. Modelo de ameaça local, não o servidor hostil remoto de D-01.
12. **Tecla Windows engolida pelo hook de teclado (C-06)**, confirmada no código atual. `Win+L` não
   bloqueia a estação local enquanto a sessão captura o teclado. O interruptor de liberação da v1.3.0a
   mitiga sob gesto do operador, mas o padrão continua capturando.
13. **Nada aqui foi validado contra um servidor TightVNC real nem contra o computador do amigo.** Os
    testes desta rodada são determinísticos, locais e offline.

---

## 6. Licença

Este documento integra o FarSightVNC, versão modificada do TightVNC, distribuída sob a **GNU General
Public License versão 2 ou posterior**. Os avisos de direito autoral e de licença do TightVNC são
preservados em todos os arquivos derivados, e os arquivos novos desta remediação trazem o mesmo
cabeçalho GPLv2-or-later.
