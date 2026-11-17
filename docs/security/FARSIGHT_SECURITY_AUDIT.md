<!-- Modified for FarSightVNC on 2026-11-13: consolidated security audit (seven independent reports + cross-review). -->

# Auditoria de segurança do FarSightVNC

Alvo: `D:\DevelopmentLibrary\Apps\FarsightVNC`, branch `develop`, HEAD `50557e9`, versão `v1.1.0a`.
Base de comparação: commit `8fd86d7` ("Import TightVNC 2.8.90 original source (baseline)").
Data: 2026-11-13.

---

## 1. Resumo executivo

**O que é grave.** Existe **uma** falha crítica, e ela está no binário entregue: o cliente de
transferência de arquivos monta o caminho local de gravação concatenando, sem nenhuma validação, o
nome de arquivo que o **servidor** enviou. Um servidor hostil (ou comprometido, ou um MITM — não há
TLS) devolve um nome como `..\..\..\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup\upd.lnk`
e o viewer grava fora da pasta escolhida pelo operador, sem diálogo nenhum, porque o diálogo de
confirmação só aparece quando o alvo **já existe**. Uma única descarga aprovada pelo operador vira
persistência na estação. Isso é herdado do TightVNC 2.8.90 — o fork não introduziu nem corrigiu —, mas
o binário é o do dono (R1: D-01/D-03/D-06).

Logo abaixo, três coisas de peso: um **vazamento de memória do processo para disco e para a tela**,
acionado por um campo de tamanho de 32 bits que não corresponde ao buffer alocado (D-02); o
**clipboard do operador enviado automaticamente ao servidor a cada cópia local**, ligado por padrão,
sem indício visual (C-02); e o fato estrutural de que **nada no transporte é cifrado ou autenticado** —
e de que o servidor pode impor o tipo de segurança `None` sem que o operador perceba (A-01/A-02/A-03).

**O que não é grave, apesar de parecer.** A revisão cruzada rebaixou cinco achados. O ponteiro de heap
aceito em `WM_USER_RECONNECT` (C-01) exige um atacante que já roda como o mesmo usuário, que já pode
substituir o `fsvncviewer.exe` na pasta do Desktop: é defesa em profundidade, não falha Alta. O
`delete[]` sobre objeto criado com `new` escalar (A-09) é comportamento indefinido real, mas não pode
ser "corrupção de heap a cada sessão", porque o produto comprovadamente abre sessões. Os achados do
atualizador (E-01/E-02) descrevem uma elevação de privilégio que **o layout de distribuição atual nem
oferece** — hoje tudo mora numa pasta gravável do Desktop. E o caminho de decodificação RFB, que é o
lugar clássico de RCE em viewers VNC, está **melhor** que o upstream: três falhas de memória herdadas
já foram corrigidas pelo fork e as validações de geometria conferem.

**O que bloqueia a versão estável.** Em ordem: (1) a validação de caminho na transferência de arquivos;
(2) o `uncompressedSize` de D-02; (3) uma política única de teto para todo campo de tamanho vindo do
fio; (4) os padrões de clipboard nas duas direções; (5) o bind do modo de escuta e o aviso de sessão
não autenticada; (6) um interruptor de transferência de arquivos no viewer (hoje quem liga o recurso é
o servidor); (7) verificação de hash antes de executar o `fsvncupdater.exe` — **antes** de existir
instalação em `Program Files`, não depois; (8) `/guard:cf` no build de shipping **e** o fim da
divergência entre os dois sistemas de build.

**Não se afirma em nenhum ponto deste documento que o FarSightVNC é seguro.** Nada foi compilado,
executado ou testado. O que existe aqui é leitura de código com evidência por arquivo:linha, revisada
de forma independente.

---

## 2. Escopo e metodologia

### O que foi feito

- **Análise estática e revisão manual** do código-fonte, somente leitura. Nenhum arquivo do
  repositório foi criado, alterado ou removido por esta auditoria, exceto este documento.
- **Comparação com o upstream** `8fd86d7` (TightVNC Viewer 2.8.90 importado como commit raiz), com
  `git diff`/`git show` por arquivo, para classificar cada achado como herdado, introduzido,
  mitigado ou compartilhado.
- **Consulta a advisories públicos** (NVD, change logs oficiais do IJG e do zlib) para as
  dependências de terceiros.
- **Seis auditorias independentes** (A a F), cada uma com escopo próprio, e uma **revisão cruzada
  independente** (G), que releu o código por conta própria para confirmar, corrigir ou refutar os
  achados de A a E.

| Relatório | Escopo |
|---|---|
| A | Núcleo RFB, negociação, autenticação, transporte |
| B | Decodificadores (Raw, CopyRect, RRE, Hextile, ZRLE, Tight, JPEG, RichCursor, DesktopSize) |
| C | Renderização, entrada e hook de teclado, clipboard, segredos locais, modo de escuta |
| D | Transferência de arquivos (`ft-client-lib`, `ft-common`, `file-lib`) |
| E | Atualizador (`farsight-updater`, `updater-lib`, `UpdateController`) |
| F | Dependências, CVEs, endurecimento de compilação e ligação |
| G | Revisão cruzada independente de A–E (F não foi revisado) |

**Regra de precedência adotada neste documento:** onde G rebaixou, corrigiu ou agrupou um achado, **o
veredito de G prevalece**. A severidade original e a revisada aparecem lado a lado na seção 7, com a
justificativa, para que a trilha continue auditável.

### O que NÃO foi feito

- **Nada foi compilado.** Todas as afirmações sobre flags de compilação vêm da leitura dos arquivos de
  projeto, não da inspeção de um binário. Flags "padrão ON" do MSVC (`/GS`, `/DYNAMICBASE`,
  `/NXCOMPAT`, `/HIGHENTROPYVA`) são **inferência** a partir do comportamento documentado do
  compilador.
- **Nada foi executado.** Nenhum crash, nenhum vazamento, nenhuma corrupção foi observada em execução.
  Onde um relatório diz "o processo morre" ou "a alocação falha", isso é dedução de código.
- **Nenhum fuzzing foi rodado.** A infraestrutura existe no repositório (`tools/fuzz-rfb-server.py`,
  `tools/fuzz-test.ps1`, `tools/zrle-cpixel-server.py`), foi **lida**, e não foi executada nem
  modificada.
- **Nenhum teste contra infraestrutura.** Nenhuma conexão de rede, nenhum servidor real, nenhum
  servidor de teste levantado, nem em loopback.
- **Nenhum binário compilado foi inspecionado** (`dumpbin`, `Get-AuthenticodeSignature`,
  `winchecksec`). A ausência de assinatura Authenticode está confirmada pela documentação e pelo
  procedimento de release, não por inspeção de PE.
- **A verificação PGP/hash do tarball do zlib 1.3.2 não foi refeita**; cita-se
  `docs/reports/fase-5-relatorio.md:14,21` como evidência documental do próprio repositório.

### Correção de premissa verificada pelo coordenador

Os auditores A–E trabalharam com a informação de que "o build CMake compila só o viewer e o updater,
logo o código de servidor nunca chega ao binário entregue". A premissa é **imprecisa**, e a correção
importa:

- `CMakeLists.txt:1-5` declara que o build **oficial** é `tightvnc2017.sln` / MSBuild
  (`tools/build.ps1`), e que o CMake é **experimental** e **não cobre os projetos de servidor**.
- O `.sln` **contém** os projetos de servidor (`tvnserver`, `rfb-sconn`, `desktop`, `ft-server-lib`,
  `http-server-lib` etc.).
- `docs/updater/RELEASING.md:28` compila apenas `updater-test, farsight-updater, tvnviewer`.

**Conclusão a registrar:** o código de servidor não chega aos binários entregues **por causa do
procedimento de release**, não porque o build o exclua. É uma garantia estritamente mais fraca: basta
alguém compilar a solução inteira, ou acrescentar um projeto à linha do `RELEASING.md`, para que
milhares de linhas nunca auditadas entrem no produto. Isso está registrado como **F-05** (ver seção 5).

**Consequência direta (de F):** uma flag de endurecimento adicionada apenas ao CMake **nunca chega ao
executável entregue**, e os dois builds **já divergiram** — `/W4 /WX` do updater existe só no CMake.

**Segunda correção verificada:** G confirmou que o CMake compila `ft-client-lib`
(`CMakeLists.txt:60-80`). Os achados de transferência de arquivos **estão no binário do viewer
entregue**; não são código morto.

### Evidência de que a revisão cruzada funcionou

G encontrou uma contradição direta entre dois relatórios: **B-12 declarou "sem achado"** ao analisar
`rfb/FrameBuffer.cpp:566-575`, exatamente as linhas onde **A-04 encontra o ponteiro pendente**
(`m_buffer` não zerado antes da realocação). Não é uma contradição de fato — B-12 avaliou
**concorrência e locking**, A-04 avaliou **tratamento de falha de alocação** — mas a redação de B-12
("resultado negativo") deixa a impressão de que aquelas linhas foram inocentadas. **A-04 prevalece: o
ponteiro pendente existe no código.** B-12 deveria ter declarado o que não cobriu.

G corrigiu também um erro de consequência que A cometeu em três achados: o `catch (...)` de
`RemoteViewerCore::execute()` (`viewer-core/RemoteViewerCore.cpp:999-1035`) é o bloco **terminal** da
função, não um `catch` dentro do laço de mensagens. Qualquer exceção ali **encerra a sessão**. Logo,
A-07/A-08 não permitem "repetir indefinidamente durante a sessão": o atacante precisa de reconexões.
B-04 estava certo.

### Testes seguros propostos (nenhum executado)

Recolhidos dos sete relatórios, todos em loopback ou local, sem rede externa, sem tocar no repositório:

| # | Alvo | Teste |
|---|---|---|
| 1 | Binário entregue | `dumpbin /headers /loadconfig` em `fsvncviewer.exe` e `fsvncupdater.exe`: conferir `Dynamic base`, `NX compatible`, `High Entropy VA`, `CF Instrumented`, `CET Compatible`, `Dependent Load Flags`. Fecha a maior limitação desta auditoria e deveria virar passo fixo do `RELEASING.md` |
| 2 | R1 / D-01 | Servidor de teste que devolve, no **segundo nível** de listagem, entradas com `..\`, caminho absoluto e UNC; verificar onde o arquivo aparece no disco |
| 3 | D-02 | `DOWNLOAD_DATA_REPLY` com `compressionLevel=0`, `compressedSize=1`, `uncompressedSize=65536`; inspecionar o arquivo gravado sob Application Verifier (páginas de heap) |
| 4 | A-04 | `DesktopSize` de 23168×23168 com o processo dentro de um Job Object limitado a ~512 MiB, para forçar `bad_alloc` de forma determinística |
| 5 | A-06 | Arquivo `.vnc` com `password=00` aberto sob Application Verifier, em diretório de rascunho (nunca no histórico real do usuário) |
| 6 | A-09 | Qualquer sessão sob Application Verifier com verificação de heap: `enableCursorShapeUpdates(true)` exercita o caminho na abertura |
| 7 | B-04 / B-07 | Cenários `zrle-huge-length` e `rre-huge-subrects` em `tools/fuzz-rfb-server.py`, classificados com `tools/fuzz-test.ps1 -DumpOnHang` |
| 8 | B-05 | JPEGs malformados em três estágios (cabeçalho, scan truncado, CMYK/YCCK), repetidos centenas de vezes sob Application Verifier, para medir vazamento por exceção |
| 9 | C-01 | `FindWindowEx(HWND_MESSAGE, NULL, L"TvnApplicationClass", NULL)` + `PostMessage(WM_USER+5, 0, 0xDEADBEEF)` sob depurador |
| 10 | C-03 | `netstat -ano \| findstr 5500` com `-listen` ativo; conectar de outra VM da rede isolada e observar se a sessão abre sem prompt |
| 11 | C-04 | `ServerInit` com `bits-per-pixel=8`, `red-max=0`; esperar `STATUS_INTEGER_DIVIDE_BY_ZERO` em `DibSection::setupBMIStruct` |
| 12 | C-06 | Com a sessão em foco, pressionar `Win+L` e verificar se a estação bloqueia; conferir se `Super_L` chegou ao servidor de teste |
| 13 | B-10 | Reconexões assistidas forçadas entre retângulos Tight, para ver se o estado zlib sobrevive |
| 14 | Geral | Harness nativo com ASan alimentando `DecoderOfRectangle::process` a partir de `ByteArrayInputStream`, cobrindo formatos de pixel que o `tvnviewer` nunca produz mas que `viewer-core` aceita — exatamente a lacuna que escondia B-01 |

---

## 3. Arquitetura e superfície de ataque

### Componentes no binário entregue

| Componente | Papel | Código |
|---|---|---|
| `tvnviewer` → `fsvncviewer.exe` | Aplicação host: janelas, shell novo, settings, clipboard, hook de teclado, modo de escuta, linha de comando | Herdado + **muito código novo do fork** (`tvnviewer/shell/`, `gui/d2d/`, `GpuPresenter`) |
| `viewer-core` | Núcleo RFB: handshake, negociação de segurança, autenticação, laço de mensagens, decodificadores | Herdado, com correções pontuais do fork |
| `rfb`, `region`, `io-lib`, `network` | Frame buffer, geometria, streams, sockets | Herdado |
| `ft-client-lib`, `ft-common`, `file-lib` | Cliente de transferência de arquivos | **Herdado, sem nenhuma mudança semântica** |
| `client-config-lib`, `config-lib` | Configuração, histórico, favoritos | Herdado + `ConnectionFavorites` (novo) |
| `util`, `win-system`, `thread`, `log-writer` | DES, inflater, clipboard Win32, hooks, registro | Herdado |
| `vendor/zlib` 1.3.2, `vendor/libjpeg` 9e | Descompressão | Terceiros, atualizados pelo fork (zlib) |
| `farsight-updater` + `updater-lib` → `fsvncupdater.exe` | Atualização automática: HTTPS, manifesto assinado, ZIP, instalação elevada | **Inteiramente novo do fork** |

Os projetos de servidor (`tvnserver`, `rfb-sconn`, `desktop`, `ft-server-lib`, `http-server-lib`,
`hookldr`, `screen-hooks`) existem na árvore e no `.sln`, mas não são compilados pelo procedimento de
release — ver a correção de premissa na seção 2 e F-05.

### Por onde entram dados do servidor

Todo byte abaixo é escolhido pelo lado remoto e processado antes de qualquer validação do operador:

1. **Pré-autenticação:** string de versão (12 bytes), lista de tipos de segurança, capacidades Tight
   (túneis e tipos de autenticação, contadores u32 sem teto), desafio de autenticação de 16 bytes,
   resultado e **razão textual de falha** (u32 sem teto → A-07).
2. **`ServerInit`:** dimensões, formato de pixel completo (`redMax`/`shift` **não validados** → C-04),
   nome do desktop (truncado em 64 KiB pelo fork → M-1).
3. **Laço de mensagens:** `FramebufferUpdate` (geometria validada; corpo de cada encoding processado
   por Raw/CopyRect/RRE/Hextile/ZRLE/Tight/JPEG), pseudo-encodings `DesktopSize` e `RichCursor`,
   `SetColourMapEntries`, `Bell`, `ServerCutText`/`ServerCutTextUtf8` (u32 sem teto útil em 64 bits).
4. **Transferência de arquivos:** `FILE_LIST_REPLY`, `DOWNLOAD_START/DATA/END_REPLY`,
   `LAST_REQUEST_FAILED_REPLY` — **nomes de arquivo, contadores e tamanhos vindos do fio**, despachados
   mesmo com a janela de FT fechada (D-08).
5. **Conexão reversa (modo de escuta):** no `-listen`, quem conduz o handshake é quem conecta — toda a
   superfície acima fica acessível a qualquer host que alcance a porta 5500 (C-03).

### Superfície local (mesmo usuário)

Janela message-only `TvnApplicationClass` aceitando ponteiro de heap em `lParam` (C-01), fila de
mensagens da janela de sessão aceitando `WM_KEYDOWN` postado (C-07), hook `WH_KEYBOARD_LL` global
instalado enquanto a sessão está ativa (C-06), diretório de staging do updater em `%LOCALAPPDATA%`
(E-01/E-02/E-06), pasta de instalação gravável no Desktop (F-01), senha em linha de comando (C-09).
**Ausências verificadas e positivas:** sem named pipe, sem `WM_COPYDATA`, sem mutex de instância única
no viewer, sem serviço, sem elevação, sem senha no registro.

---

## 4. Comparação de segurança com o TightVNC 2.8.90

| Componente | TightVNC original | FarSightVNC | Evidência | Risco |
|---|---|---|---|---|
| **Protocolo RFB** | RFB 3.3/3.7/3.8; validação de versão restritiva; geometria de retângulo conferida; `ServerInit` com `vector<char>(size+1)` sobre u32 → vetor vazio e escrita/leitura de ~4 GiB | **Idêntico, com uma correção real**: nome do desktop truncado em 64 KiB, `read()` → `readFully()`, excedente drenado em blocos de 4 KiB. Tipo Tight ainda ecoado em RFB 3.3 (desvio) | `viewer-core/RemoteViewerCore.cpp:1379-1400` (M-1, commit `d698379`); `:674-690` (A-15); `:1313-1337` (validação de versão) | **Menor** que o upstream no pré-sessão; empatado no resto |
| **Autenticação** | VNC Authentication (DES, senha truncada em 8 caracteres), desafio escolhido pelo servidor, **sem autenticação do servidor**; `None` aceito na ordem enviada pelo servidor, sem aviso | **Igual, sem mudança nenhuma.** `rfb/AuthDefs.h` e `VncAuthentication.cpp` sem commits desde `8fd86d7` | `viewer-core/VncAuthentication.cpp:39-55`; `util/VncPassCrypt.h:38`; `RemoteViewerCore.cpp:750-756`; `tools/fake-rfb-server.py:199-201` (o servidor de teste do próprio projeto depende de `None`) | **Igual** — A-02, A-03 |
| **Criptografia** | Nenhuma. Sem TLS, sem VeNCrypt, sem MAC. Senha salva com chave DES fixa pública | **Nenhuma.** `SecurityDefs` define apenas `INVALID/NONE/VNC/TIGHT`; cadeia de streams montada direto sobre o socket. Mesma chave DES fixa `{23,82,107,6,35,78,88,7}` | `rfb/AuthDefs.h:31-38`; `viewer-core/TcpConnection.cpp:129-133`; `util/VncPassCrypt.cpp:29` | **Igual** — A-01, A-05 |
| **Decodificação** | ZRLE com CPIXEL de 3 bytes escrevendo além do fim de `vector<unsigned int>`; `fillRect` escrevendo uma linha após o buffer com retângulo de altura zero; `setColor` em laço infinito com `pixelSize == 0` | **Três correções reais, verificadas por G no código** (não só no `MODIFICATIONS.md`): guarda de layout + `expandPixel`/`putPixel` com limites; `if (clipRect.isEmpty()) return;`; early-return em `setColor`. Validações de geometria, índice de paleta, comprimento de run e teto de descompressão conferidas e corretas | `viewer-core/ZrleDecoder.cpp:165-203`; `rfb/FrameBuffer.cpp:95-100`, `:70-72`; `viewer-core/TightDecoder.cpp:292-300`; `viewer-core/RemoteViewerCore.cpp:1143-1166` | **Melhor** que o upstream (B-01/B-02/B-03 fechados) |
| **Clipboard** | Bidirecional automático, uma única opção para os dois sentidos, ligada por padrão; comprimento u32 com verificação inócua em 64 bits | **Mesmo comportamento e mesmo padrão.** O fork **recriou a tela de opções** e manteve uma única caixa para as duas direções; acrescentou o caminho UTF-8 e o teste de overflow de comprimento | `tvnviewer/DesktopWindow.cpp:329-345`, `:348-354`; `client-config-lib/ConnectionConfig.cpp:34-42`; `tvnviewer/shell/SettingsSections.cpp:316`; `RemoteViewerCore.cpp:1229-1235` | **Igual** (oportunidade perdida no redesign) — C-02, C-05, A-08 |
| **Transferência de arquivos** | Nome de arquivo do servidor concatenado ao caminho local sem validação; `uncompressedSize` usado sobre buffer de `compressedSize`; `truncate()` que apaga antes de recriar; despacho sem máquina de estados | **Idêntico, byte a byte.** Nenhum `.cpp` de lógica de FT aparece no diff contra `8fd86d7` — só `.h` (remoção de `throw()`), `.vcxproj` e o caption da janela | `git diff --stat 8fd86d7 HEAD -- ft-client-lib ft-common file-lib`; `ft-client-lib/FileTransferReplyBuffer.cpp:136-147`, `:252-269`; `DownloadOperation.cpp:112-121`, `:146-152`; `file-lib/File.cpp:335-343` | **Igual — e é aqui que está a falha crítica** |
| **Atualizações** | Não existe atualizador. Binários oficiais **assinados com Authenticode** ("OOO GlavSoft") | **Atualizador novo, criptograficamente sólido**: ECDSA P-256 sobre SHA-256 via CNG, assinatura verificada antes do parse, hash do zip e de cada arquivo conferido em quatro pontos, HTTPS obrigatório, zip-slip impossível por construção, kill switch por política. **Mas**: binários **sem Authenticode** (regressão), e o executável que instala e eleva é lançado de `%LOCALAPPDATA%` sem verificação de integridade | `updater-lib/Manifest.cpp:232-258`; `UpdateService.cpp:199-296`; `Installer.cpp:475`, `:492`; `docs/updater/DESIGN.md:44`; `tvnviewer/UpdateController.cpp:687-728` | **Melhor e pior ao mesmo tempo**: canal novo bem feito (F-02 atenuado), procedência do PE pior que o upstream |
| **Dependências** | zlib **1.2.13**, libjpeg IJG 9e, D3DES; `contrib/minizip` ausente; toolset `v141_xp` + SDK 8.1 | **zlib 1.3.2** (release corrente, 17-Fev-2026, incorpora os achados da auditoria da 7ASecurity), libjpeg 9e inalterado, D3DES idêntico; **zero modificações locais** nas dependências; toolset **v145 + SDK 10.0**. Updater **sem nenhuma dependência de terceiros** (crypto/JSON/ZIP próprios) | `vendor/zlib/zlib.h:44`; `vendor/libjpeg/jversion.h:12`; `docs/reports/fase-5-relatorio.md:14,21`; `tvnviewer/tvnviewer.vcxproj:41,48-81`; `updater-lib/Crypto.h:1-5` | **Melhor.** Nenhum CVE avaliado permanece aplicável |
| **Permissões** | Sem manifesto próprio do viewer; `/GS`, ASLR, DEP nos padrões; sem CFG, sem `SetDefaultDllDirectories` | Viewer **sem `requestedExecutionLevel`** (vale `asInvoker`), sem CFG, sem CET, sem `HeapSetInformation`, sem defesa anti-DLL-planting, rodando de pasta gravável no Desktop. **Updater** com `asInvoker` explícito, `supportedOS`, `/DEPENDENTLOADFLAG:0x800` e `SetDefaultDllDirectories` | `tvnviewer/dpi-aware.manifest`; `farsight-updater/farsight-updater.manifest:3-9,14-25`; `farsight-updater/main.cpp:518`; `cmake/FsvTargets.cmake:14-18` | **Pior no viewer** (o fork demonstrou conhecer a mitigação e não a estendeu), **melhor no updater** — F-01, F-03, F-04 |

---

## 5. Vulnerabilidades confirmadas

Critério de inclusão: achados que G manteve como **CONFIRMADO-REVISADO** (severidade original ou
corrigida), ou achados que já eram CONFIRMADO em seu relatório de origem e que G não contestou (não
revisou e não refutou). Causas-raiz repetidas em vários relatórios aparecem **uma vez**, com todos os
identificadores originais referenciados, conforme a orientação de G (seção "Causas-raiz agrupadas").
Os 13 campos pedidos aparecem em cada item; onde um campo não se aplica (ex.: achado sem CVSS por não
ter vetor remoto próprio), isso é declarado explicitamente, não omitido.

### D-01 (+ D-03, D-06) — Escrita/exclusão arbitrária de arquivo por directory traversal na descarga [R1]

1. **Identificador:** D-01 (crítico), com D-03 e D-06 absorvidos como a mesma causa-raiz (R1 de G).
2. **Componente:** `ft-client-lib` (cliente de transferência de arquivos), alcançável no binário
   entregue (G confirmou: `CMakeLists.txt:60-80` compila `ft-client-lib`).
3. **Descrição técnica:** o nome de cada entrada de `FILE_LIST_REPLY` é lido por
   `FileTransferReplyBuffer.cpp:136-147` (`readUTF8` → `setFileName`) sem qualquer filtro;
   `FileInfo::setFileName` (`ft-common/FileInfo.cpp:81-84`) apenas armazena a string; `getAbsolutePath`
   (`ft-client-lib/FileInfoList.cpp:149-174`) concatena pasta-pai + separador + nome; `getLocalPath`
   (`FileTransferOperation.cpp:107-118`) anexa isso à pasta escolhida pelo operador, sem canonicalização
   nem comparação de prefixo. `DownloadOperation::processFile()`/`processFolder()`
   (`DownloadOperation.cpp:272-334`) chamam `File::truncate()` (`file-lib/File.cpp:335-343`:
   `remove()` seguido de `createNewFile()`, sem checar o retorno) e `CreateFile` no caminho resultante.
   O diálogo de confirmação (`FileExistDialog`) só aparece quando o alvo **já existe**
   (`DownloadOperation.cpp:273-312`); como o atacante escolhe um alvo novo, a gravação é silenciosa.
4. **Origem:** **herdado**. `git diff --stat 8fd86d7 HEAD -- ft-client-lib ft-common file-lib` não
   toca nenhum `.cpp` de lógica — só `.h` (remoção de `throw()`), `.vcxproj` e o caption da janela.
5. **Evidência no código:** `ft-client-lib/FileTransferReplyBuffer.cpp:136-147`;
   `ft-common/FileInfo.cpp:81-84`; `ft-client-lib/FileInfoList.cpp:149-174`;
   `ft-client-lib/FileTransferOperation.cpp:107-118`; `ft-client-lib/DownloadOperation.cpp:112-121,
   272-334`; `file-lib/File.cpp:335-343`; `tvnviewer/FileExistDialog.cpp:45-51`.
6. **Cenário de exploração:** operador conecta a um servidor hostil/comprometido/MITM, abre a janela de
   File Transfer (listagem remota automática ao abrir — D-08), seleciona uma pasta aparentemente
   inócua e clica *Download*. O servidor devolve, no primeiro nível, um diretório de nome normal; ao
   descer um nível, devolve entradas com `..\..\..\Users\<vítima>\...\Startup\upd.lnk` (ou caminho
   absoluto, ou UNC). O `..` é resolvido pelo Win32 e o arquivo é criado fora da pasta escolhida, sem
   diálogo, porque o alvo não existia.
7. **Pré-requisitos:** uma única ação de descarga aprovada pelo operador; nenhum privilégio adicional;
   nenhuma corrida.
8. **Impacto potencial:** escrita/sobrescrita arbitrária de arquivo no contexto do usuário, incluindo
   caminhos de autostart — execução de código persistente na estação; variantes incluem sobrescrever o
   próprio `fsvncviewer.exe`/`fsvncupdater.exe`, ADS, nomes reservados DOS.
9. **Severidade:** **Crítica**. CVSS 4.0 estimado `AV:N/AC:L/AT:N/PR:N/UI:A/VC:N/VI:H/VA:H` ≈ 8,5.
   D-03 (exclusão/TOCTOU) e D-06 (diálogo que não protege) são a mesma primitiva — G rebaixou-os a
   **Média** quando avaliados isoladamente, porque não existem como falha independente sem D-01.
10. **Recomendação de mitigação:** validar `FileInfo::getFileName()` no parse (rejeitar vazio, `.`,
    `..`, `\`, `/`, `:`, nomes reservados DOS, ponto/espaço final) **e** canonicalizar o caminho final
    com `GetFullPathName`, exigindo que seja prefixo da pasta escolhida; tornar o padrão do
    `FileExistDialog` *Skip* em vez de *Overwrite*; limitar "Overwrite All" ao lote selecionado pelo
    operador.
11. **Impacto na compatibilidade:** nenhum com servidores TightVNC legítimos — o protocolo transporta
    apenas *nome de entrada*, nunca caminho; o servidor TightVNC já filtra `.`/`..` na própria
    enumeração.
12. **Necessidade de intervenção no servidor:** não — mitigação inteiramente no viewer.
13. **Bloqueador da versão estável:** **sim, o principal**.

### D-02 — Leitura fora dos limites: `uncompressedSize` usado sobre buffer de `compressedSize`

1. **Identificador:** D-02 (parte do padrão R2 de G, junto com D-05).
2. **Componente:** `ft-client-lib/FileTransferReplyBuffer.cpp`, `DownloadOperation.cpp`.
3. **Descrição técnica:** `readCompressedDataBlock` (`FileTransferReplyBuffer.cpp:252-269`) aloca
   `vector<UINT8> coBuffer(coSize)` e, quando `compressionLevel == 0`, retorna esse buffer **sem
   consultar** `uncoSize`; mas `m_downloadBufferSize` é atribuído a `uncoBufferSize`
   (`:186-191`), e `DownloadOperation.cpp:146-152` grava `m_downloadBufferSize` bytes a partir desse
   buffer de `coSize` bytes. A mesma dessincronia ocorre em `FILE_LIST_REPLY` (`:120-124`:
   `ByteArrayInputStream(..., uncompressedSize)` sobre buffer de `compressedSize`).
4. **Origem:** **herdado**. `git diff --stat 8fd86d7 HEAD -- ft-client-lib/FileTransferReplyBuffer.cpp
   io-lib/ByteArrayInputStream.cpp` vazio; só o `.h` perdeu `throw(...)`.
5. **Evidência no código:** `ft-client-lib/FileTransferReplyBuffer.cpp:186-191, 252-269`;
   `ft-client-lib/DownloadOperation.cpp:146-152` (G corrigiu a linha citada pelo relatório original,
   que dizia `:149-159`).
6. **Cenário de exploração:** descarga em andamento; servidor envia `DOWNLOAD_DATA_REPLY` com
   `compressionLevel=0`, `compressedSize=1`, `uncompressedSize=65536` e 1 byte de payload — o viewer
   grava ~64 KB de heap adjacente no arquivo local (pode conter senha VNC em claro, clipboard,
   fragmentos de framebuffer, ponteiros). Com `uncompressedSize=0xFFFF0000`, leitura de ~4 GB fora do
   heap gera violação de acesso (DoS determinístico). Variante sem descarga: em `FILE_LIST_REPLY` os
   mesmos bytes alimentam nomes de arquivo exibidos na UI — vazamento direto na tela, e retroalimenta
   D-01.
7. **Pré-requisitos:** uma descarga (ou apenas a listagem automática ao abrir a janela — D-08).
8. **Impacto potencial:** divulgação de memória do processo (bypass de ASLR, vazamento de credencial)
   para disco e para a UI; crash remoto confiável. É leitura, não escrita — não é RCE direto.
9. **Severidade:** **Alta**. CVSS 4.0 `AV:N/AC:L/AT:N/PR:N/UI:A/VC:H/VI:N/VA:L` ≈ 7,0.
10. **Recomendação de mitigação:** quando `compressionLevel == 0`, exigir
    `uncompressedSize == compressedSize` (ou usar `coBuffer.size()` como fonte da verdade); construir o
    `ByteArrayInputStream` de `FILE_LIST_REPLY` com `buffer.size()`.
11. **Impacto na compatibilidade:** nenhum — um servidor TightVNC correto já envia os dois campos
    iguais quando não comprime.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** **sim**.

### C-02 — Clipboard do operador enviado ao servidor a cada cópia local, ligado por padrão

1. **Identificador:** C-02.
2. **Componente:** `tvnviewer/DesktopWindow.cpp` (`onDrawClipboard`), `client-config-lib/ConnectionConfig.cpp`.
3. **Descrição técnica:** a janela de sessão se inscreve na cadeia de visualizadores de clipboard do
   Windows; a cada `WM_DRAWCLIPBOARD` o texto é lido com `WinClipboard::getString()` e enviado ao
   servidor como `ClientCutText`, sem confirmação. A única condição é a janela estar visível e
   `isClipboardEnabled()`, cujo padrão (`m_isClipboardEnabled(true)`) está ligado.
4. **Origem:** **herdado** no comportamento e no padrão; o fork **recriou a tela de opções**
   (`tvnviewer/shell/SettingsSections.cpp`, arquivo novo) mantendo uma única caixa para as duas
   direções, sem separar envio/recebimento e sem mudar o padrão.
5. **Evidência no código:** `tvnviewer/DesktopWindow.cpp:329-345`; `client-config-lib/ConnectionConfig.cpp:39`;
   `tvnviewer/shell/SettingsSections.cpp:316`.
6. **Cenário de exploração:** operador conecta a servidor malicioso/comprometido (ou aceita conexão
   reversa via modo de escuta, C-03) e, durante a sessão, copia qualquer coisa na própria máquina —
   senha de gerenciador, token, trecho de documento. O conteúdo vai ao servidor sem indício visível.
7. **Pré-requisitos:** sessão ativa, janela de sessão visível, operador copia algo localmente.
8. **Impacto potencial:** exfiltração contínua de dados do clipboard corporativo — a via de vazamento
   mais provável do produto.
9. **Severidade:** **Alta**. CVSS 4.0 `AV:N/AC:L/AT:P/PR:N/UI:P/VC:H/VI:N/VA:N` ≈ 6,3–7,0.
10. **Recomendação de mitigação:** separar as duas direções em opções distintas; desligar o envio por
    padrão e/ou exigir gesto explícito ("enviar clipboard").
11. **Impacto na compatibilidade:** nenhum — `ClientCutText` é opcional no RFB.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** **sim**, junto com C-05 (mesma caixa, sentido inverso — servidor
    sobrescreve o clipboard local sem consentimento, `RemoteViewerCore.cpp:1221-1275` →
    `DesktopWindow::setClipboardData`, Média, CONFIRMADO).

### C-03 — Modo de escuta: bind em `0.0.0.0:5500` sem autenticação, confirmação ou `SO_EXCLUSIVEADDRUSE`

1. **Identificador:** C-03.
2. **Componente:** `tvnviewer/ConnectionListener.cpp`, `network/TcpServer.cpp`.
3. **Descrição técnica:** no `-listen` o viewer abre um socket em todas as interfaces, porta 5500,
   `TcpServer(DEFAULT_HOST="0.0.0.0", port, autoStart=true)` — o quarto parâmetro `lockAddr` fica no
   default `false`, então `setExclusiveAddrUse()` nunca é chamado. Ao aceitar, `newListeningConnection()`
   cria imediatamente uma janela de shell e uma `ViewerInstance`: sem lista de hosts permitidos, sem
   caixa de confirmação. Na conexão reversa é o par remoto quem conduz o handshake RFB e escolhe o
   tipo de segurança — inclusive `None`.
4. **Origem:** **herdado**; `git diff 8fd86d7 HEAD` nos três arquivos citados é vazio. Novidade do fork:
   porta editável na tela de Settings e cada conexão de entrada ganha janela própria.
5. **Evidência no código:** `tvnviewer/ConnectionListener.cpp:30,34`; `network/TcpServer.cpp:36-47`;
   `network/socket/SocketIPv4.cpp:317-321` (`setExclusiveAddrUse` existe, não é chamado);
   `tvnviewer/TvnViewer.cpp:473-495`.
6. **Cenário de exploração:** operador liga o modo de escuta (uso de suporte típico). Qualquer host da
   LAN (ou da Internet, com NAT/port-forward) conecta na 5500, apresenta-se como servidor, escolhe
   `None`, e exercita todo o parser RFB, C-02/C-05 e C-04.
7. **Pré-requisitos:** modo de escuta ativado pelo operador (não é o padrão de conexão de saída).
8. **Impacto potencial:** porta não autenticada em máquina corporativa; ponto de entrada de rede para
   todos os demais achados.
9. **Severidade:** **Alta enquanto a escuta está ativa; Média no ciclo de vida do produto** (não é o
   padrão). CVSS 4.0 com escuta ativa `AV:A/AC:L/AT:N/PR:N/UI:N/VC:L/VI:L/VA:L` ≈ 6,9.
10. **Recomendação de mitigação:** bind em `127.0.0.1` por padrão, com opção explícita para
    `0.0.0.0`; caixa de confirmação mostrando o IP de origem; lista de redes/hosts permitidos;
    `lockAddr = true`.
11. **Impacto na compatibilidade:** nenhum — servidores TightVNC de conexão reversa continuam
    funcionando, apenas passam a depender de aprovação do operador.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** **sim** (ao menos o aviso explícito de "sessão não autenticada").

### A-01, A-02, A-03 — Ausência de cifragem/integridade; rebaixamento silencioso a `None`; senha exposta ao desafio do servidor

1. **Identificador:** A-01 (Alta), A-02 (Média isolada/Alta em conjunto), A-03 (Alta) — tratados juntos
   porque compõem um único modelo de ameaça de transporte.
2. **Componente:** `viewer-core/TcpConnection.cpp`, `rfb/AuthDefs.h`, `viewer-core/RemoteViewerCore.cpp`,
   `viewer-core/VncAuthentication.cpp`, `util/VncPassCrypt.h`.
3. **Descrição técnica:** (A-01) a cadeia de streams é montada direto sobre o socket TCP
   (`SocketIPv4 → SocketStream → BufferedInputStream → RfbInputGate/RfbOutputGate`); `SecurityDefs`
   define só `INVALID/NONE/VNC/TIGHT` — nenhum tipo com TLS. Todo o tráfego pós-autenticação (tela,
   teclado, mouse, clipboard, transferência de arquivos) viaja em claro e sem MAC. (A-02)
   `selectSecurityType` percorre a lista **na ordem do servidor** e aceita o primeiro tipo com
   tratador ou que seja `NONE`; não há política do cliente nem aviso de sessão não autenticada. (A-03)
   a senha é truncada a 8 caracteres, usada como chave DES, e o cliente responde ao desafio de 16
   bytes **antes** de qualquer prova de identidade do servidor — o desafio é escolhido pelo lado
   remoto, permitindo ataque offline com texto conhecido.
4. **Origem:** **herdado** nos três — propriedade estrutural do RFB 3.8 / VNC Authentication (RFC 6143
   §7.2.2); nenhum dos arquivos citados tem commits de alteração semântica desde `8fd86d7`.
5. **Evidência no código:** `viewer-core/TcpConnection.cpp:111-133`; `rfb/AuthDefs.h:31-38`;
   `viewer-core/RemoteViewerCore.cpp:733-761,646-691`; `viewer-core/VncAuthentication.cpp:32-57`;
   `util/VncPassCrypt.h:38`; confirmação empírica em `tools/fake-rfb-server.py:199-201`, que já
   depende de `None` ser aceito.
6. **Cenário de exploração:** posição na rota (ARP/DNS spoofing, Wi-Fi hostil, roteador comprometido)
   entre viewer e servidor sem túnel SSH/VPN (A-01); ou servidor hostil no endereço esperado que
   anuncia só `[None]` (A-02) ou coleta o par desafio/resposta na primeira tentativa de autenticação
   (A-03) — nenhum dos dois exige MITM.
7. **Pré-requisitos:** A-01 exige posição na rota; A-02/A-03 bastam um servidor hostil no endereço
   esperado, sem MITM.
8. **Impacto potencial:** sequestro de sessão completo (A-01); operador conectado sem saber que a
   sessão não é autenticada (A-02); recuperação offline da senha VNC do operador — reutilizável em
   outros hosts (A-03).
9. **Severidade:** A-01 **Alta** (`AV:N/AC:H/AT:P/PR:N/UI:N/VC:H/VI:H/VA:N`); A-02 **Média isolada,
   Alta em conjunto com A-01** (`AV:N/AC:H/AT:P/PR:N/UI:P/VC:L/VI:H/VA:N`); A-03 **Alta**
   (`AV:N/AC:L/AT:N/PR:N/UI:P/VC:H/VI:N/VA:N`).
10. **Recomendação de mitigação:** A-01/A-03 são limitação de protocolo — mitigação completa só fora do
    viewer (túnel SSH/VPN, gateway TLS já previsto no roadmap do projeto, ou VeNCrypt se o servidor
    oferecer, o que o TightVNC Server não faz). No viewer: recusar `None` por padrão, avisar
    explicitamente quando a sessão não é autenticada, TOFU de identidade de servidor antes de enviar
    senha salva, `SecureZeroMemory` nos buffers de senha em claro.
11. **Impacto na compatibilidade:** adicionar aviso/opção é mudança de comportamento visível, não de
    protocolo; servidores TightVNC sem senha passariam a exigir confirmação única.
12. **Necessidade de intervenção no servidor:** **sim, para a correção completa** (TLS/VeNCrypt exige
    suporte do servidor, que o TightVNC Server não tem); a mitigação de aviso/política é só no viewer.
13. **Bloqueador da versão estável:** o aviso de sessão não autenticada (A-02) e a limpeza de memória
    (A-03/A-14), **sim**; a cifragem completa (A-01), **não** — é risco documentado e aceito via
    túnel externo, não algo que bloqueia o build atual.

### R3 — Nenhum limite superior em contadores/tamanhos u32 vindos do fio

1. **Identificador:** A-07, A-08, A-10, B-04, B-07, D-05 — mesmo padrão, agrupado por G.
2. **Componente:** `io-lib/DataInputStream.cpp` (A-07, razão de autenticação), `viewer-core/RemoteViewerCore.cpp`
   (A-08, clipboard; A-10, capacidades Tight), `viewer-core/ZrleDecoder.cpp` (B-04, bloco zlib),
   `viewer-core/RreDecoder.cpp` (B-07, sub-retângulos RRE), `ft-client-lib` (D-05, contagem de arquivos).
3. **Descrição técnica:** em todos os seis pontos um campo u32 vindo do servidor dimensiona uma
   alocação ou um laço **antes** de qualquer validação relacionada ao que de fato será recebido.
   Exemplos: `DataInputStream::readUTF8` aloca `vector<char>(sizeInBytes)` com `sizeInBytes` até
   `0xFFFFFFFF` (A-07); `ServerCutText`/`ServerCutTextUtf8` testam `length > SIZE_MAX - 1`, que só
   protege builds de 32 bits — o fork é só 64 bits (A-08); `ZrleDecoder::readAndInflate` usa `length`
   direto em `resize()` antes de receber o payload, **ignorando** o teto `maximalUnpackedSize` já
   calculado e passado como parâmetro (B-04); `RreDecoder` usa `numberRectangle` como contador de laço
   sem comparação com a área do retângulo (B-07); `FileTransferCore::executeOperation` redimensiona
   `m_remoteFilesInfo` para `numberFiles` sem teto (D-05).
4. **Origem:** **herdado** nos seis — nenhum arquivo citado tem alteração semântica desde `8fd86d7`.
5. **Evidência no código:** `io-lib/DataInputStream.cpp:129-139`; `viewer-core/RemoteViewerCore.cpp:1229-1260`
   (A-08), `:763-835` (A-10); `viewer-core/ZrleDecoder.cpp:205-218` (B-04); `viewer-core/RreDecoder.cpp:41-50`
   (B-07); `ft-client-lib/FileTransferCore.cpp:134-137`, `ft-client-lib/FileTransferReplyBuffer.cpp:131-133`,
   `ft-client-lib/UploadOperation.cpp:536-543` (D-05).
6. **Cenário de exploração:** servidor malicioso anuncia um campo de tamanho/contagem extremo (razão de
   falha de autenticação, área de clipboard, bloco ZRLE comprimido, contagem de sub-retângulos RRE,
   contagem de arquivos) sem enviar o corpo correspondente.
7. **Pré-requisitos:** nenhum privilégio; alguns pontos são pré-autenticação (A-07, A-10).
8. **Impacto potencial:** exaustão de memória / `bad_alloc` (DoS); em B-04 a mitigação já existente
   (`maximalUnpackedSize`) é ignorada por descuido, o que é o item de correção mais barata do grupo.
9. **Severidade:** **Baixa a Média-baixa** em cada ponto individual (B-04 é o mais perto de Média,
   pois ignora um teto já calculado); nenhum CVSS alto, pois nenhum é corrupção de memória — é DoS.
10. **Recomendação de mitigação:** política única: todo tamanho vindo do fio tem teto explícito,
    proporcional ao que o protocolo de fato permite (ex.: limitar ao `maximalUnpackedSize` já existente
    em B-04; alguns KiB para a razão de autenticação; teto proporcional à área do retângulo em RRE), e
    o excedente é consumido em blocos, nunca alocado de uma vez.
11. **Impacto na compatibilidade:** nenhum — nenhum servidor TightVNC real excede esses tetos.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** **sim, como política única** (prioridade alta por ser barata e
    cobrir seis achados de uma vez).

### R5 — Liberação/alocação sem RAII no caminho de falha

1. **Identificador:** A-04 (`resizeBuffer`), A-09 (`delete[]`), B-10 (`TightDecoder::reset`), B-11
   (`WatermarksController`) — agrupados por G como o mesmo padrão de projeto.
2. **Componente:** `rfb/FrameBuffer.cpp`, `viewer-core/DecoderStore.cpp`, `viewer-core/TightDecoder.cpp`,
   `viewer-core/WatermarksController.cpp`.
3. **Descrição técnica:** (A-04) `FrameBuffer::resizeBuffer()` faz `delete[] m_buffer;` e então
   `m_buffer = new UINT8[...]`, sem zerar `m_buffer` entre as duas operações; se o `new` lançar
   `bad_alloc`, `m_buffer` fica pendente. G corrigiu a consequência: o `catch(...)` que captura essa
   exceção em `RemoteViewerCore::execute()` (`:999-1035`) é o bloco **terminal**, logo a sessão é
   encerrada — o risco dominante é **double free no destrutor**, não UAF de longa duração com UI viva.
   (A-09) `DecoderStore::addDecoder` libera decodificador duplicado com `delete[]` sobre objeto criado
   com `new` escalar — defeito real, mas G observa que, se o comportamento do MSVC fosse o pior caso
   (vector deleting destructor lendo cookie inexistente), **toda sessão derrubaria o viewer na
   abertura**, e o produto comprovadamente funciona; logo não é "corrupção a cada sessão". (B-10)
   `TightDecoder::reset()`/`resetDecoders()` fazem `delete m_inflater[i]; m_inflater[i] = new Inflater;`
   sem proteção — se o `new` lançar, o ponteiro fica pendente. (B-11) `WatermarksController::loadFromfile()`
   vaza o buffer anterior a cada chamada, mas está inteiramente sob `#ifdef _DEMO_VERSION_`, nunca
   definido em nenhum projeto de build — **código morto confirmado**.
4. **Origem:** **herdado** nos quatro.
5. **Evidência no código:** `rfb/FrameBuffer.cpp:565-574`; `viewer-core/DecoderStore.cpp:92-101`;
   `viewer-core/TightDecoder.cpp:127-142`; `viewer-core/WatermarksController.cpp:107-146`.
6. **Cenário de exploração:** A-04 exige `DesktopSize` grande seguido de pressão de memória real
   (teto atual permite até ~2 GiB por buffer, dois buffers por redimensionamento); A-09 é **local**,
   exercitado em toda sessão via `enableCursorShapeUpdates(true)` (padrão ligado); B-10 exige falha de
   alocação na janela exata do reset; B-11 não é alcançável (código morto).
7. **Pré-requisitos:** variam — A-09 não exige servidor; A-04/B-10 exigem pressão de memória real.
8. **Impacto potencial:** double free / UAF em cenário de falha de alocação (A-04, B-10); UB de
   liberação em A-09, sem crash demonstrado; nenhum em B-11.
9. **Severidade:** A-04 **Média** (teto frouxo citado, confiança reduzida por G: forçar `bad_alloc`
   abaixo de 2 GiB não é trivial em x64); A-09 **Baixa** (corrigida por G, de Média); B-10 **Baixa**
   (pré-condição difícil); B-11 **Informativa** (código morto).
10. **Recomendação de mitigação:** `std::unique_ptr` resolve os quatro — atribuir só após o `new`
    suceder, nunca deixar um ponteiro-membro pendente entre `delete` e `new`.
11. **Impacto na compatibilidade:** nenhum.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** A-04, **sim** (teto de `DesktopSize` mais apertado, 16384×16384
    em vez de ~2 GiB); A-09, recomendado mas não bloqueador; B-10/B-11, não bloqueadores.

### R6 — Binário/estado do atualizador consumido sem verificação de integridade

1. **Identificador:** E-01, E-02, E-03 — mesma regra de correção, agrupados por G.
2. **Componente:** `tvnviewer/UpdateController.cpp`, `farsight-updater/main.cpp`, `updater-lib/Installer.cpp`.
3. **Descrição técnica:** (E-01) o viewer copia `fsvncupdater.exe` para
   `%LOCALAPPDATA%\FarSightVNC\Updates\<hash>\bin\` e executa a cópia, sem comparar hash com o
   manifesto assinado, sem `WinVerifyTrust` (não há Authenticode), com janela TOCTOU entre o
   `CopyFileW` e o `CreateProcessW`; o processo assim iniciado decide se precisa de admin e, em caso
   afirmativo, relança **a si mesmo** (`currentModulePath()`) com `runas`. (E-02) se `binExe` já
   existir, é executado **sem cópia e sem verificação alguma** — é pior que E-01 por não depender de
   corrida, melhor por exigir um layout sem `fsvncupdater.exe` ao lado do viewer (que não é o layout
   entregue). (E-03) `rollbackOne`/`recoverInstall` restauram `backup\<rel>` com `MoveFileExW` sem
   comparar tamanho ou hash, guiados por um journal de texto validado só sintaticamente — assimetria
   real com o caminho de instalação, que verifica SHA-256 duas vezes.
4. **Origem:** **introduzido pelo fork** nos três — código inteiramente novo (não existe atualizador
   no TightVNC 2.8.90).
5. **Evidência no código:** `tvnviewer/UpdateController.cpp:687-728`; `farsight-updater/main.cpp:291-299`;
   `updater-lib/Installer.cpp:275-302, 684-727`.
6. **Cenário de exploração (G, rebaixado para o contexto entregue):** atacante local do mesmo usuário
   substitui o binário na janela de corrida (E-01) ou pré-planta `fsvncupdater.exe` (E-02). **Mas**: o
   layout de shipping atual (`CLAUDE.md`: `Desktop\FarSightVNC-v<versão>\`) já é uma pasta gravável
   pelo usuário — o mesmo atacante já pode substituir `fsvncviewer.exe` diretamente, sem cruzar
   fronteira alguma. E-01/E-02 só se tornam o elo mais fraco **quando houver instalação em diretório
   protegido** (`Program Files`), que o produto ainda não suporta.
7. **Pré-requisitos:** E-01/E-02, atacante local do mesmo usuário **e** (para o cenário Média-Alta)
   instalação futura em diretório protegido; E-03, escrita local em `backup\`, inexplorável sob ACL
   padrão de `Program Files`.
8. **Impacto potencial:** elevação local de privilégio de usuário-padrão/usuário-admin-com-UAC para
   administrador, com pretexto de UI legítimo (quando aplicável); restauração de arquivo adulterado em
   rollback (E-03).
9. **Severidade:** E-01 **Baixa hoje; Média-Alta se/quando houver instalação em diretório protegido**
   (rebaixado por G do Média-Alta original); E-02 **Média** (rebaixado de Média-Alta); E-03 **Média**
   (mantido).
10. **Recomendação de mitigação:** verificar o hash de `binExe` contra o manifesto assinado
    imediatamente antes do `CreateProcessW`; assinar `fsvncupdater.exe` com Authenticode e chamar
    `WinVerifyTrust` antes de executar/elevar; revalidar `currentModulePath()` contra o hash do
    manifesto no processo elevado; verificar hash também no rollback.
11. **Impacto na compatibilidade:** nenhum — é verificação adicional, não muda formato de pacote.
12. **Necessidade de intervenção no servidor:** não (é local ao viewer/updater).
13. **Bloqueador da versão estável:** **sim, antes de qualquer instalação em diretório protegido** — a
    condição correta é "antes do instalador", não "depois", exatamente como G recomendou.

### R7 — Segredo do viewer protegido só por ofuscação DES de chave pública fixa

1. **Identificador:** A-05, A-06, C-09 — mesmo segredo, agrupados por G.
2. **Componente:** `util/VncPassCrypt.cpp`, `tvnviewer/ConnectionData.cpp`, `tvnviewer/ViewerCmdLine.cpp`.
3. **Descrição técnica:** (A-05) a senha salva em arquivo `.vnc`/configuração é DES com chave constante
   pública embutida no binário (`{23,82,107,6,35,78,88,7}`), reversível por qualquer um que leia o
   arquivo. (A-06) `getPlainPassword()` indexa `ansiHidePassword.getString()[i*2]`/`[i*2+1]` para
   `i=0..7` **sem verificar o comprimento** do campo, e trata um buffer de 8 bytes sem NUL como string
   C — um `.vnc` com `password=00` causa leitura fora dos limites, possivelmente exfiltrando bytes de
   heap adjacentes para a senha enviada ao servidor. G rebaixou a severidade: quem controla o `.vnc`
   malicioso já controla host/porta/senha da conexão; o ganho é no máximo ~8-14 bytes de heap.
   (C-09) a mesma ofuscação se repete na linha de comando (`-password=`, visível a qualquer processo
   do mesmo usuário via `Win32_Process.CommandLine`) e no arquivo de opções.
4. **Origem:** **herdado** nos três.
5. **Evidência no código:** `util/VncPassCrypt.cpp:29`; `tvnviewer/ConnectionData.cpp:119-165`;
   `tvnviewer/ViewerCmdLine.cpp:40,214-254`.
6. **Cenário de exploração:** leitura do arquivo `.vnc`/config (backup, pasta sincronizada, outro
   usuário da máquina) revela a senha em claro (A-05); abertura de um `.vnc` malicioso com campo
   `password` curto causa leitura OOB (A-06); uso de `-password=` expõe a credencial a qualquer
   processo local (C-09).
7. **Pré-requisitos:** acesso de leitura ao arquivo, ou execução local como o mesmo usuário.
8. **Impacto potencial:** vazamento da senha VNC em claro, reutilizável em outros hosts.
9. **Severidade:** A-05 **Média**; A-06 **Baixa-Média** (rebaixado por G de Média); C-09 **Baixa a
   Média**, dependendo do uso.
10. **Recomendação de mitigação:** `CryptProtectData` (DPAPI, escopo do usuário) para o campo salvo,
    mantendo leitura do formato antigo para migração; validar que o campo hex tem exatamente 16
    caracteres antes de decodificar, com buffer de 9 bytes e NUL explícito; aceitar senha por
    stdin/variável de ambiente/arquivo com ACL em vez de argumento de linha de comando.
11. **Impacto na compatibilidade:** DPAPI tornaria o `.vnc` não portátil entre máquinas/usuários (é o
    objetivo) e quebraria interoperabilidade de arquivo de senha com o TightVNC Viewer original —
    decisão explícita do dono; o restante não afeta compatibilidade.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** A-06 (correção de uma validação de comprimento), **sim**; A-05/C-09,
    recomendados, não bloqueadores.

### C-01 — Ponteiro de heap aceito em `WM_USER_RECONNECT`/`WM_USER_HOME_CONNECT`

1. **Identificador:** C-01.
2. **Componente:** `tvnviewer/TvnViewer.cpp`, `win-system/WindowsApplication.cpp`.
3. **Descrição técnica:** a janela principal message-only (`TvnApplicationClass`) trata
   `WM_USER_RECONNECT`/`WM_USER_HOME_CONNECT` fazendo `reinterpret_cast<ReconnectRequest*>(lparam)`,
   lendo os campos e, via `std::unique_ptr`, chamando `delete` no ponteiro — sem validação, token ou
   verificação de origem. A janela é localizável por `FindWindowEx(HWND_MESSAGE, ...)` e
   `PostMessage` entre processos de mesma integridade é permitido pelo UIPI.
4. **Origem:** **compartilhado**. O upstream já passava dois ponteiros crus na mesma mensagem, sem
   liberar nenhum; o fork reduziu para um ponteiro mas **introduziu** a liberação (`delete` de
   endereço vindo de fora) e **acrescentou** uma segunda mensagem com o mesmo formato.
5. **Evidência no código:** `tvnviewer/TvnViewer.cpp:566-584`; `win-system/WindowsApplication.cpp:75-83`.
6. **Cenário de exploração:** malware/binário do mesmo usuário localiza a janela e posta a mensagem com
   um endereço escolhido.
7. **Pré-requisitos (G, decisivo para a severidade):** o atacante já executa como o mesmo usuário,
   mesma sessão, mesmo nível de integridade. Nesse contexto ele já pode `OpenProcess(PROCESS_ALL_ACCESS)`
   no viewer, injetar DLL, ou substituir `fsvncviewer.exe` (pasta gravável no Desktop) — não há
   fronteira de privilégio do Windows sendo cruzada.
8. **Impacto potencial:** corrupção de memória no processo do viewer; defesa em profundidade, não
   escalonamento de privilégio.
9. **Severidade:** **Baixa-Média** (rebaixado por G do Alta original de 7,3 — o próprio relatório
   original admitia "não atravessa fronteira de privilégio" mas ainda atribuía `VC:H/VI:H/VA:H`).
10. **Recomendação de mitigação:** substituir o ponteiro por um identificador inteiro em tabela interna
    preenchida só pelo próprio processo; nunca passar ponteiros em mensagens de janela.
11. **Impacto na compatibilidade:** nenhum — é mudança interna de IPC local.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** não é bloqueador crítico, mas é correção barata e recomendada
    antes do beta.

### C-04 — Divisão por zero com formato de pixel de 8 bpp e `redMax`/`greenMax`/`blueMax` não validados

1. **Identificador:** C-04.
2. **Componente:** `win-system/DibSection.cpp`, `viewer-core/RemoteViewerCore.cpp`.
3. **Descrição técnica:** `readServerInit()` adota o formato de pixel do servidor validando apenas
   `width/height`, `bitsPerPixel ∈ {8,16,32}` e o teto de tamanho — os campos `redMax`/`greenMax`/
   `blueMax`/`*Shift` não são validados. `DibSection::setupBMIStruct`, no ramo de 8 bpp, divide por
   `pf->redMax`/`greenMax`/`blueMax`; com `bitsPerPixel=8` e `redMax=0` ocorre divisão inteira por
   zero — exceção de hardware do Windows, não capturada pelos `catch (const Exception&)` do caminho de
   pintura, que derruba o processo.
4. **Origem:** **herdado**, com mitigações parciais introduzidas pelo fork em volta (validação de
   `bitsPerPixel`/dimensão, teto `fitsFrameBuffer`) que não cobrem este campo.
5. **Evidência no código:** `win-system/DibSection.cpp:109-124`; `viewer-core/RemoteViewerCore.cpp:1370-1378,
   842-846`.
6. **Cenário de exploração:** servidor malicioso (ou par do modo de escuta) responde ao `ServerInit`
   com `bits-per-pixel=8`, `red-max=0`.
7. **Pré-requisitos:** nenhum privilégio; é parte do handshake inicial.
8. **Impacto potencial:** negação de serviço do viewer (queda do processo, perda de todas as sessões
   abertas na mesma instância multi-shell).
9. **Severidade:** **Média**. CVSS 4.0 `AV:N/AC:L/AT:N/PR:N/UI:P/VC:N/VI:N/VA:H` ≈ 6,3.
10. **Recomendação de mitigação:** rejeitar em `setFbProperties()` qualquer formato com
    `redMax==0 || greenMax==0 || blueMax==0` ou `*Shift >= bitsPerPixel`; alternativamente, nunca
    criar o DIB local com o formato do servidor.
11. **Impacto na compatibilidade:** nenhum — servidores TightVNC reais nunca anunciam `*Max=0`.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** **sim** (correção trivial, DoS determinístico).

### C-06 — Hook de teclado engole modificadores e a tecla Windows (`Win+L` deixa de bloquear a estação)

1. **Identificador:** C-06.
2. **Componente:** `win-system/WinHooks.cpp` (`decideKeyAction`).
3. **Descrição técnica:** a função de decisão nova deixa passar ao Windows apenas eventos injetados e
   as teclas de travamento/Print Screen; **todo o resto — inclusive `VK_LWIN`/`VK_RWIN`, `Ctrl`, `Alt`,
   `Shift` — é repostado para a janela da sessão e o evento original consumido**. O upstream devolvia
   explicitamente os modificadores ao Windows.
4. **Origem:** **introduzido pelo fork** (a lista de exclusões de modificadores do upstream
   desapareceu na reescrita de `WinHooks.cpp`/`KeyRouter`).
5. **Evidência no código:** `win-system/WinHooks.cpp:108-118`.
6. **Cenário de exploração:** não é ataque de terceiro — é perda de controle de segurança da estação.
   Com a sessão em foco, `Win+L` é enviado ao servidor remoto e a estação local permanece desbloqueada.
7. **Pré-requisitos:** sessão em primeiro plano com o teclado.
8. **Impacto potencial:** bloqueio de tela por atalho neutralizado durante a sessão; hook `WH_KEYBOARD_LL`
   de todo o sistema é também primitiva de keylogger pronta caso outro código já esteja no processo.
9. **Severidade:** **Média** (segurança operacional, não falha de memória; sem CVSS por não haver
   atacante remoto direto).
10. **Recomendação de mitigação:** devolver `VK_LWIN`/`VK_RWIN` ao Windows por padrão (ou só capturar
    em tela cheia, com aviso); opção explícita "enviar atalhos do Windows ao servidor".
11. **Impacto na compatibilidade:** nenhuma mudança de protocolo RFB — apenas menos keysyms de
    modificador enviados.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** recomendado para ambiente corporativo, não bloqueador técnico.

### D-04 — Desreferência nula em resposta de transferência fora de ordem [parte de R4]

1. **Identificador:** D-04 (agrupado com A-11/C-12 como R4 por G — mesma raiz de "despacho sem máquina
   de estados nem teto", problemas distintos).
2. **Componente:** `ft-client-lib/FileTransferMessageProcessor.cpp`, `DownloadOperation.cpp`.
3. **Descrição técnica:** o despacho de mensagens de FT é dirigido só pelo código da mensagem, sem
   consultar estado; `DownloadOperation` inicializa `m_file(0), m_fos(0)` e, se `DOWNLOAD_END_REPLY`
   chegar antes de `start()`, `m_fos->close()` é chamado sobre nulo. G demonstrou a alcançabilidade:
   `FileTransferCore::executeOperation` registra a operação como listener **antes** de chamá-la, logo
   o servidor pode responder fora de ordem desde o primeiro byte.
4. **Origem:** **herdado/compartilhado**.
5. **Evidência no código:** `ft-client-lib/FileTransferMessageProcessor.cpp:36-88`;
   `ft-client-lib/DownloadOperation.cpp:33-37,192-200`; `ft-client-lib/FileTransferCore.cpp:186-211`.
6. **Cenário de exploração:** servidor malicioso responde `DOWNLOAD_END_REPLY` imediatamente após o
   início de uma descarga.
7. **Pré-requisitos:** operador inicia uma descarga.
8. **Impacto potencial:** crash do processo do viewer (violação de acesso); todos os `delete` são
   seguidos de `= NULL`, então não há corrupção explorável adicional.
9. **Severidade:** **Média** (rebaixado por G do Alto original — um servidor que já controla a sessão
   pode derrubar o viewer de outras formas igualmente baratas).
10. **Recomendação de mitigação:** máquina de estados explícita no despacho de FT; checar nulo antes
    de `m_fos->close()`.
11. **Impacto na compatibilidade:** nenhum.
12. **Necessidade de intervenção no servidor:** não.
13. **Bloqueador da versão estável:** não é bloqueador crítico; recomendado.

---

## 6. Riscos plausíveis e hipóteses pendentes

Separados dos confirmados porque a confiança declarada pelos relatórios é **PLAUSÍVEL** ou
**HIPÓTESE** — o defeito de código pode estar confirmado, mas a consequência explorável não foi
observada em execução.

| Risco | O que falta para confirmar |
|---|---|
| **B-05** — exceção C++ lançada de dentro de `error_exit` do libjpeg, `jpeg_destroy` duplo, sem checagem de `output_components` | Executar JPEGs malformados (cabeçalho inválido, scan truncado, CMYK/YCCK) sob Application Verifier com heap completo, repetindo centenas de vezes, para medir vazamento por exceção e confirmar o comportamento real do desenrolar C++ através de quadros C no MSVC |
| **A-16** — corridas de dados residuais entre threads de rede/entrada/UI (`m_output` sem lock em `execute()`; `close()` concorrente com `recv()`; `m_forceFullUpdate` sem mutex) | Nenhuma via de exploração identificada; teste de estresse de abertura/fechamento de sessão sob ThreadSanitizer ou Application Verifier com detecção de corrida |
| **B-10** (estado zlib residual entre conexões) | Responder se a reconexão assistida (`tvnviewer/ReconnectPolicy.h`, `ViewerInstance.cpp`) recria `RemoteViewerCore`/`DecoderStore` — nenhum relatório verificou isso |
| **A-09** (caminho remoto de capacidade duplicada) | Confirmado como **latente**: `addEncodingCapability()` nunca é chamado em nenhum ponto do repositório; permanece hipótese até que algum código futuro registre capacidades de codificação |
| **D-09** (hipótese de controle do caminho local de upload pelo servidor) | **Refutada** por D: o fluxo de dados completo de `UploadOperation.cpp`/`FolderListener.cpp` mostra que o caminho local vem só da UI. Mantida aqui apenas como exemplo de hipótese corretamente descartada com evidência |
| **C-07** (injeção local de entrada via `PostMessage` na janela de sessão) | O código confia na fila de mensagens — confirmado; a injeção **entre processos** diferentes não foi demonstrada |
| **C-10/C-11** (cadeia de clipboard nunca removida; contagem errada em `GetPriorityClipboardFormat`; `GlobalLock` sem verificação) | Defeitos de API confirmados; impacto explorável é hipótese — exigiria repetir o ciclo de abrir/fechar sessões sob monitoramento de handles |
| **E-08** (processo elevado valida `--state` só pela forma do caminho) | Sem caminho de ataque autônomo identificado — exigiria encontrar outro processo que consiga induzir o viewer a passar um `--state` malicioso ao updater elevado |
| **F-01** (DLL planting via `LoadLibrary` cru em `gui/d2d/Theme.cpp`, `tvnviewer/shell/ShellWindow.cpp`) | Testar plantio real de `dwmapi.dll`/`uxtheme.dll` na pasta do executável e consultar a lista `KnownDLLs` da máquina alvo — não verificado |
| **Causa-raiz geral** | Nenhuma hipótese de corrupção de memória remotamente explorável (A-04, A-09, B-05, B-10) foi observada em execução; todas dependem de fuzzing com ASan ou Application Verifier, que esta auditoria não executou |

---

## 7. Matriz de severidade

Todos os achados dos sete relatórios, com severidade final conforme a seção 5/6 (após revisão de G
quando aplicável), confiança e se bloqueia a versão estável.

| ID | Título | Componente | Origem | Severidade final | Confiança | Bloqueador? |
|---|---|---|---|---|---|---|
| D-01/D-03/D-06 | Directory traversal na descarga (R1) | ft-client-lib | Herdado | Crítica | Confirmado | **Sim** |
| D-02 | `uncompressedSize` sobre buffer de `compressedSize` | ft-client-lib | Herdado | Alta | Confirmado | **Sim** |
| C-02/C-05 | Clipboard automático nas duas direções | tvnviewer | Herdado | Alta | Confirmado | **Sim** |
| C-03 | Modo de escuta sem autenticação | tvnviewer/network | Herdado | Alta (ativo) / Média (ciclo de vida) | Confirmado | **Sim** |
| A-01 | Sem cifragem/integridade no transporte | viewer-core/rfb | Herdado | Alta | Confirmado | Parcial (aviso, sim; cifragem, não) |
| A-02 | `None` aceito sem aviso | viewer-core | Herdado | Média isolada / Alta em conjunto | Confirmado | **Sim** (aviso) |
| A-03 | Senha exposta ao desafio do servidor | viewer-core/util | Herdado | Alta | Confirmado | Parcial (limpeza de memória, sim) |
| A-04 | `resizeBuffer` com ponteiro pendente | rfb | Herdado | Média (corrigida por G) | Confirmado (defeito) / Plausível (exploração) | **Sim** (teto) |
| A-05 | Senha salva com DES de chave fixa | util/tvnviewer | Herdado | Média | Confirmado | Recomendado |
| A-06 | Leitura OOB em `getPlainPassword` | tvnviewer | Herdado | Baixa-Média (corrigida por G) | Confirmado | **Sim** |
| A-07 | Alocação de até 4 GiB (razão de autenticação) | io-lib | Herdado | Baixa (parte de R3) | Confirmado | **Sim** (política) |
| A-08 | Clipboard: teste de overflow inócuo em 64 bits | viewer-core | Herdado | Baixa (parte de R3) | Confirmado | **Sim** (política) |
| A-09 | `delete[]` sobre `new` escalar | viewer-core | Herdado | Baixa (corrigida por G) | Confirmado (defeito) | Recomendado |
| A-10 | Contadores u32 sem limite (Tight) | viewer-core | Herdado | Baixa (parte de R3) | Confirmado | **Sim** (política) |
| A-11/C-12 | Fila de entrada sem limite superior | viewer-core | **Introduzido** | Baixa | Confirmado | Recomendado |
| A-12 | Relançamento por valor perde tipo `IOException` | io-lib | Herdado | Informativa | Confirmado | Não |
| A-13 | `isdigit()` com `char` negativo | viewer-core | Herdado | Informativa | Confirmado (UB) / Hipótese (efeito) | Não |
| A-14 | Senha em claro não zerada na memória | viewer-core/tvnviewer | Herdado | Informativa | Confirmado | Recomendado |
| A-15 | Tipo Tight ecoado em RFB 3.3 | viewer-core | Herdado | Informativa | Confirmado (desvio) | Não |
| A-16 | Corridas de dados residuais entre threads | viewer-core | Misto (introduzido/herdado) | Baixa/Informativa | Confirmado (corrida formal) / Hipótese (impacto) | Não |
| B-01 | ZRLE CPIXEL 3 bytes — heap corruption | viewer-core | **Mitigado** | Nenhuma hoje (era Alta) | Confirmado | Regressão a vigiar |
| B-02 | `fillRect` escreve 1 linha após o buffer | rfb | **Mitigado** | Nenhuma hoje (era Alta) | Confirmado | Regressão a vigiar |
| B-03 | `setColor` laço infinito com `pixelSize==0` | rfb | **Mitigado** | Nenhuma hoje (era Média) | Confirmado | Regressão a vigiar |
| B-04 | ZRLE: alocação não limitada do bloco comprimido | viewer-core | Herdado | Média-baixa (parte de R3) | Confirmado | **Sim** (trivial) |
| B-05 | libjpeg: exceção em callback, `jpeg_destroy` duplo | viewer-core | Herdado | Média | Confirmado (padrão) / Plausível (crash) | Recomendado |
| B-06 | Hextile: tile não preenchido sem background | viewer-core | Herdado | Baixa | Confirmado | Não |
| B-07 | RRE: `number-of-subrectangles` sem limite | viewer-core | Herdado | Baixa (parte de R3) | Confirmado | **Sim** (política) |
| B-08 | Dessincronização silenciosa (filtro Tight/ZRLE) | viewer-core | Herdado | Baixa | Confirmado (comportamento) | Recomendado |
| B-09 | `_ASSERT` de truncamento só em debug | util | Herdado | Baixa/Informativa | Hipótese (inalcançável hoje) | Não |
| B-10 | `TightDecoder::reset`/`m_inflater` sem RAII | viewer-core | Herdado | Baixa (parte de R5) | Hipótese | Não |
| B-11 | Vazamento em `WatermarksController` | viewer-core | Compartilhado | Informativa (código morto) | Confirmado | Não |
| B-12 | `FbUpdateNotifier`/`CursorPainter` — sem achado de concorrência | viewer-core | Mitigado (formal) | Informativa | Confirmado (locking) | Não |
| C-01 | Ponteiro de heap em `WM_USER_RECONNECT` | tvnviewer | Compartilhado (agravado) | Baixa-Média (corrigida por G) | Confirmado | Recomendado |
| C-04 | Divisão por zero (formato de pixel 8 bpp) | win-system/viewer-core | Herdado | Média | Confirmado | **Sim** |
| C-06 | Hook engole `Win+L` e modificadores | win-system | **Introduzido** | Média | Confirmado | Recomendado |
| C-07 | Entrada injetável localmente por `PostMessage` | tvnviewer | Compartilhado | Média | Plausível | Não |
| C-08 | Clipboard gravado em disco com log nível 9 | viewer-core | Herdado | Baixa | Confirmado | Não |
| C-09 | Senha em linha de comando / arquivo de opções | tvnviewer | Herdado | Baixa a Média (parte de R7) | Confirmado | Recomendado |
| C-10 | Cadeia de clipboard viewers nunca removida | win-system | Herdado (agravado) | Baixa | Confirmado (ausência) / Plausível (quebra) | Não |
| C-11 | `WinClipboard`: contagem/`GlobalLock`/`GlobalFree` incorretos | win-system | Herdado | Baixa | Confirmado (API) / Hipótese (impacto) | Não |
| C-13 | Nome do desktop do servidor exibido sem marcação (spoofing de UI) | viewer-core | Mitigado quanto ao tamanho | Baixa | Confirmado (caminho) / Hipótese (efeito) | Não |
| D-02 | (ver seção 5) | — | — | — | — | — |
| D-04 | Resposta FT fora de ordem — desreferência nula | ft-client-lib | Herdado/Compartilhado | Média (corrigida por G) | Confirmado | Recomendado |
| D-05 | Contadores/tamanhos FT sem teto | ft-client-lib | Herdado (parte de R3) | Média | Confirmado (ausência de teto) / Plausível (vazamento) | **Sim** (política) |
| D-07 | Segue reparse points; sem DACL restritiva | file-lib | Herdado | Média | Confirmado (flags) / Hipótese (encadeamento) | Recomendado |
| D-08 | FT alcançável por padrão, habilitada pelo servidor | viewer-core | Herdado | Média (amplificador) | Confirmado | **Sim** |
| D-09 | Upload: hipótese de controle do caminho local | ft-client-lib | Herdado | Baixa (refutada como primitiva) | Confirmado (refutação) | Não |
| D-10 | Conversão UTF-8→UTF-16: cast para `int` | io-lib/util | Herdado | Baixa | Hipótese | Não |
| E-01 | Updater copiado para pasta gravável, sem verificação | tvnviewer/farsight-updater | **Introduzido** | Baixa hoje / Média-Alta com instalação protegida (corrigida por G) | Confirmado (fato) / Plausível (exploração completa) | **Sim** (antes de instalador) |
| E-02 | Fallback executa updater pré-plantado | tvnviewer | **Introduzido** | Média (corrigida por G) | Confirmado | **Sim** (antes de instalador) |
| E-03 | Rollback sem verificação de hash | updater-lib | **Introduzido** | Média | Confirmado | Recomendado |
| E-04 | Verificação de downgrade ignorável; sem proteção de replay por tempo | farsight-updater/updater-lib | **Introduzido** | Média (contexto corporativo) | Confirmado | Recomendado |
| E-05 | Sem pinning de certificado/chave TLS | updater-lib | **Introduzido** | Baixa | Confirmado | Não |
| E-06 | Usuário local pode desarmar atualizações | farsight-updater | **Introduzido** | Baixa | Confirmado | Não |
| E-07 | Raiz de confiança: uma única chave ECDSA | updater-lib | **Introduzido** | Baixa como código / Crítica como risco operacional | Confirmado | Recomendado (chave de reserva) |
| E-08 | `--state` validado só pela forma | farsight-updater | **Introduzido** | Baixa | Confirmado (validação) / Hipótese (exploração) | Não |
| E-09 | Viewer instalado sem as defesas anti-DLL-planting do updater | tvnviewer | Herdado (assimetria introduzida) | Baixa | Confirmado | Recomendado |
| E-10 | Falha ao consultar espaço livre tratada como "há espaço" | updater-lib | **Introduzido** | Informativa | Confirmado | Não |
| E-11 | Parser JSON manual — nenhuma falha de memória encontrada | updater-lib | **Introduzido** | Informativa (achado negativo) | Confirmado | Não |
| E-12 | `ProcessScan` não mata processos | updater-lib | — | Informativa (confirmação de design) | Confirmado | Não |
| F-01 | Viewer sem defesa anti-DLL-planting; pasta gravável | tvnviewer/gui | Misto | Média-Alta | Confirmado (ausência) / Plausível (exploração) | **Sim** |
| F-02 | Sem Authenticode (regressão do upstream) | shipping | Regressão consciente | Média | Confirmado | Recomendado (mitigado parcialmente pela assinatura do manifesto) |
| F-03 | Control Flow Guard desligado | build | Herdado (viável desde o retarget) | Média (gap) | Confirmado | **Sim** |
| F-04 | `SetProcessMitigationPolicy`/`HeapSetInformation` ausentes | build | Herdado | Baixa-Média (gap) | Confirmado | Recomendado |
| F-05 | Dois sistemas de build divergentes | processo | **Introduzido** | Baixa-Média (risco de processo) | Confirmado | **Sim** |
| F-06 | `winhdr.h` ainda fixa `_WIN32_WINNT 0x0501` | build | Herdado (incoerente após retarget) | Baixa (habilitador) | Confirmado | Recomendado |
| F-07 | `/Qspectre`, `/sdl`, `/CETCOMPAT`, `/ZH:SHA_256` ausentes | build | Herdado | Baixa (gap) | Confirmado | Recomendado |
| F-08 | Viewer em `/W3` sem `/WX` (updater em `/W4 /WX`) | build | Assimetria introduzida | Baixa (qualidade de processo) | Confirmado | Recomendado |
| F-09 | Manifesto do viewer sem `requestedExecutionLevel` | tvnviewer | **Introduzido** (incompleto) | Informativa/Baixa | Confirmado | Recomendado |
| F-10 | libjpeg 9e vs 9f — sem impacto de segurança | vendor | Herdado | Informativa | Confirmado | Não |
| F-11 | `vendor/zlib` cópia parcial — atrito de proveniência | vendor | Herdado | Informativa | Confirmado | Não |

---

## 8. Riscos para ambientes corporativos

### A — Servidor comprometido

Um servidor TightVNC legítimo comprometido (ou um técnico de suporte que conecta a um host hostil por
engano) controla **todo** o parser RFB e o cliente de FT. O caminho de maior impacto é D-01: uma única
descarga aprovada pelo operador grava arquivo arbitrário fora da pasta escolhida, incluindo locais de
autostart — persistência imediata na estação. Em segundo lugar, C-02 exfiltra o clipboard do operador
continuamente, sem que ele perceba. A-04/B-04/B-07/R3 permitem degradar ou derrubar a sessão. Nenhum
caminho de decodificação RFB demonstra corrupção de memória explorável no estado atual (B-01/B-02/B-03
já fechados), o que é o ponto positivo deste cenário.

### B — Interceptação/MITM

Sem TLS (A-01) e com rebaixamento silencioso a `None` possível (A-02), um MITM ativo tem o mesmo poder
que um servidor hostil, mais a capacidade de alterar o tráfego de uma sessão que o operador acredita
legítima. Combinado com C-05 (clipboard sobrescrito pelo servidor) e C-02 (clipboard exfiltrado), o
MITM pode injetar comandos no clipboard do operador e receber de volta dados sensíveis, sem que nada
na UI sinalize a ausência de autenticação. Mitigação real só fora do viewer (túnel SSH/VPN, gateway
TLS) — o TightVNC Server não oferece VeNCrypt.

### C — Movimento lateral

Um atacante que já executa código como o mesmo usuário (phishing, macro, outro malware) encontra, no
processo do viewer: C-01 (ponteiro de heap aceito via mensagem de janela — defesa em profundidade, não
fronteira de privilégio), C-09 (senha visível em `Win32_Process.CommandLine` se usada por linha de
comando), e o diretório de staging do updater em `%LOCALAPPDATA%` (E-01/E-02/E-06). Nenhum desses
cruza uma fronteira de privilégio do Windows por si só — o atacante que já está no mesmo nível de
integridade tem caminhos mais diretos (substituir o próprio `.exe` na pasta gravável do Desktop). O
risco real de movimento lateral aparece **somente quando o produto evoluir para instalação em
diretório protegido** (`Program Files`), situação em que E-01/E-02/E-03 passam de Baixa/Média a
Média-Alta — ver seção 5 (R6).

### D — Atualizador comprometido

A cadeia de assinatura ECDSA P-256 + verificação de hash em quatro pontos é sólida e **não tem
bypass conhecido** no código (E-11 confirma ausência de falha de memória no parser JSON manual que a
alimenta). O risco real não está na criptografia: está em (1) o binário que de fato executa e eleva
(`fsvncupdater.exe`) ser lançado de pasta gravável sem verificação de hash própria (E-01/E-02); (2) a
ausência de Authenticode, que elimina o sinal de procedência do SO para quem obtém o produto fora do
fluxo de update (F-02); (3) a raiz de confiança ser uma única chave ECDSA sem revogação — comprometer
a máquina de desenvolvimento do dono compromete todo o parque instalado (E-07, risco crítico
**operacional**, não de código).

### E — Exposição de serviços locais / modo de escuta

O `-listen` em `0.0.0.0:5500` sem autenticação, confirmação ou `SO_EXCLUSIVEADDRUSE` (C-03) é o único
caminho deste conjunto que transforma a máquina do operador em superfície de rede acessível por
qualquer host da LAN (ou da Internet, com NAT). Uma vez conectado, o par remoto tem acesso completo ao
parser RFB, ao clipboard nas duas direções e ao formato de pixel — exatamente o conjunto dos cenários
A e B, mas iniciado pela rede em vez de por uma ação de conectar. Não é o padrão do produto (exige
`-listen`), mas quando ativo a severidade é Alta.

---

## 9. Limitações herdadas do protocolo RFB

Nada abaixo tem conserto possível só no viewer — são propriedades do RFB/VNC Authentication em si:

- **DES de 56 bits efetivos com senha truncada em 8 caracteres** (RFC 6143 §7.2.2). O viewer pode
  avisar sobre o limite e evitar reenvio automático para hosts não confirmados, mas não pode trocar o
  algoritmo sem deixar de interoperar com qualquer servidor TightVNC existente.
- **Ausência de TLS/criptografia no TightVNC Server.** O protocolo define o tipo de segurança Tight
  (que suportaria VeNCrypt), mas o TightVNC Server não o implementa. Adicionar suporte a VeNCrypt no
  viewer (avaliado em `docs/seguranca/avaliacao-vencrypt.md`) só protegeria conexões a servidores que
  o ofereçam — nenhum servidor TightVNC real o oferece hoje.
- **Autenticação só do cliente.** O RFB 3.8 nunca exige que o servidor prove identidade antes do
  cliente responder ao desafio — é o desenho do protocolo, não uma escolha de implementação.
- **Negociação de segurança conduzida pelo servidor.** A lista de tipos de segurança vem do servidor,
  na ordem que ele escolhe; o cliente pode recusar tipos fracos (mitigação no viewer, A-02), mas não
  pode forçar o servidor a oferecer algo que ele não suporta.

A mitigação efetiva para os quatro pontos acima, hoje, é **externa ao viewer**: túnel SSH/VPN, ou o
gateway TLS já previsto no roadmap do projeto (`docs/seguranca/tunel-ssh-vpn.md`), não uma correção de
código no FarSightVNC.

---

## 10. Recomendações priorizadas

Ordem de G: **D-01/D-03 (R1) → D-02 → C-02 → A-01+A-02+A-03 (transporte) → C-03 → R3 (tetos) → E
(R6) → R5 → o resto.** Cada item com esforço, risco de regressão, compatibilidade com servidores
TightVNC existentes e se é correção só-no-viewer.

| # | Recomendação | Esforço | Risco de regressão | Compatível com servidores TightVNC? | Só-no-viewer ou depende de infraestrutura? |
|---|---|---|---|---|---|
| 1 | Validar nome de arquivo no parse de FT + canonicalizar caminho final (D-01/D-03/D-06) | Médio | Baixo (só recusa nomes que o Windows já não representa) | Sim, total | Só-no-viewer |
| 2 | Corrigir `uncompressedSize`/`compressedSize` em FT (D-02) | Baixo | Nenhum | Sim, total | Só-no-viewer |
| 3 | Separar clipboard em duas opções, desligar envio por padrão ou exigir gesto (C-02/C-05) | Baixo | Médio (muda comportamento padrão esperado por usuários atuais) | Sim | Só-no-viewer |
| 4 | Recusar `None` por padrão + aviso de sessão não autenticada (A-02) | Médio | Médio (servidores sem senha passam a exigir confirmação) | Sim | Só-no-viewer |
| 5 | Túnel SSH/VPN ou gateway TLS para cifragem real (A-01) | Alto | Baixo (aditivo) | Sim | **Depende de infraestrutura** (gateway) ou de VeNCrypt no servidor |
| 6 | Bind padrão em `127.0.0.1` + confirmação de origem no modo de escuta (C-03) | Baixo | Baixo | Sim (conexão reversa continua funcionando) | Só-no-viewer |
| 7 | Teto único para todo tamanho/contador vindo do fio (R3: A-07/A-08/A-10/B-04/B-07/D-05) | Médio | Nenhum | Sim | Só-no-viewer |
| 8 | Verificar hash do `fsvncupdater.exe` contra manifesto assinado antes de executar/elevar (E-01/E-02/E-03) | Médio | Baixo | N/A (não é protocolo RFB) | Só-no-viewer/updater |
| 9 | `std::unique_ptr` nos quatro pontos de alocação sem RAII (R5: A-04/A-09/B-10/B-11) | Baixo | Baixo | Sim | Só-no-viewer |
| 10 | DPAPI para senha salva + validação de comprimento do campo hex (R7: A-05/A-06/C-09) | Médio | Médio (quebra portabilidade de `.vnc` entre máquinas, se DPAPI) | Parcial — decisão do dono | Só-no-viewer |
| 11 | Interruptor de FT no viewer, padrão desligado/perguntar (D-08) | Baixo | Baixo | Sim (não anunciar capability é mecanismo previsto) | Só-no-viewer |
| 12 | `redMax`/`greenMax`/`blueMax`/`*Shift` validados em `setFbProperties` (C-04) | Baixo | Nenhum | Sim | Só-no-viewer |
| 13 | `/guard:cf` no viewer e nas bibliotecas estáticas, nos dois sistemas de build (F-03) | Médio | Médio (requer teste de fumaça completo, `GetProcAddress` em `gui/d2d/Theme.cpp`) | N/A | Só-no-viewer |
| 14 | Unificar/sincronizar os dois sistemas de build; passo de verificação do binário no `RELEASING.md` (F-05) | Médio | Nenhum | N/A | Processo |
| 15 | `HeapSetInformation(HeapEnableTerminateOnCorruption)` + `SetDefaultDllDirectories` no viewer (F-01/F-04) | Baixo | Baixo (pode expor bug latente de heap como crash visível, que é o comportamento desejado) | N/A | Só-no-viewer |
| 16 | Devolver `VK_LWIN`/`VK_RWIN` ao Windows por padrão no hook (C-06) | Baixo | Baixo | Sim | Só-no-viewer |
| 17 | Chave de reserva do updater mantida offline (E-07) | Médio | Nenhum (código já suporta múltiplas chaves) | N/A | Processo/infraestrutura |
| 18 | Authenticode nos dois executáveis (F-02) | Alto (custo recorrente de certificado) | Nenhum | N/A | Processo/infraestrutura |

---

## 11. Impactos sobre a compatibilidade

Regra fundamental do projeto: o FarSightVNC precisa continuar funcionando nos mesmos ambientes, sem
exigir mudança no servidor, na rede ou no esquema de autenticação do VNC. Avaliação de cada
recomendação da seção 10 contra essa regra:

- **Recomendações 1, 2, 7, 9, 11, 12, 16** não têm nenhum impacto de compatibilidade: são validações
  que um servidor TightVNC legítimo já satisfaz, ou mudanças internas de gestão de memória/IPC.
- **Recomendações 3, 4, 6** mudam **comportamento visível** (uma caixa de diálogo adicional, um
  clipboard que para de ser automático, um bind que passa a ser local por padrão) mas não mudam o
  protocolo RFB nem exigem nada do servidor. Servidores TightVNC existentes continuam funcionando;
  usuários percebem a diferença de UX e precisam ser avisados na nota de versão.
- **Recomendação 5** (cifragem real) só é viável sem mudança de servidor através de infraestrutura
  externa (túnel, gateway) — não há forma de o viewer impor TLS a um TightVNC Server que não o
  suporta, sem deixar de interoperar com o parque existente.
- **Recomendação 10** (DPAPI) é a única com impacto real de compatibilidade **declarado**: um `.vnc`
  com senha protegida por DPAPI deixa de ser portável entre máquinas/usuários e deixa de ser
  intercambiável com o TightVNC Viewer original. É mudança desejável de segurança que exige decisão
  explícita do dono (manter leitura do formato antigo resolve a migração, não a interoperabilidade
  bidirecional).
- **Recomendações 13–18** são de build/processo/infraestrutura: nenhuma altera o protocolo RFB ou a
  interoperabilidade com servidores TightVNC. O risco está em regressão de **funcionalidade do
  viewer** (CFG pode interagir mal com os casts de `GetProcAddress` em `gui/d2d/Theme.cpp` — exige
  teste de fumaça), não em compatibilidade de protocolo.

Nenhuma recomendação desta lista exige alterar o `tvnserver` nem qualquer servidor TightVNC de
terceiros — a regra do dono está preservada em todos os 18 itens.

---

## 12. Critérios de segurança para entrada em beta

Antes de abrir o produto para um grupo maior de testadores (beta), as condições mínimas verificadas
por esta auditoria são:

1. D-01/D-03/D-06 corrigidos (validação de nome de arquivo e canonicalização de caminho na FT).
2. D-02 corrigido (`uncompressedSize` não pode mais exceder `compressedSize` real).
3. C-04 corrigido (divisão por zero determinística eliminada).
4. R3 aplicado (teto único para tamanhos/contadores vindos do fio) — ao menos nos seis pontos listados.
5. Aviso explícito de "sessão não autenticada" quando o tipo de segurança negociado é `None` (A-02).
6. R5 aplicado (RAII nos quatro pontos de alocação sem proteção).
7. A regra de build dupla resolvida: ou um único sistema de build gera o shipping, ou todo
   endurecimento futuro é aplicado nos dois simultaneamente, com verificação do binário real
   (F-05 + teste 1 da seção 2).
8. `/guard:cf` no build de shipping, com teste de fumaça completo do caminho GPU/tema.
9. Clipboard: ao menos a separação de opções (envio/recebimento), mesmo que o padrão não mude ainda.

Esta auditoria **não afirma** que a lista acima é suficiente para considerar o produto seguro — é o
piso mínimo para reduzir o risco de que um testador de beta sofra persistência de arquivo, vazamento
de memória para disco, ou DoS determinístico ao conectar a um servidor hostil.

---

## 13. Bloqueadores da primeira versão estável

Em ordem de prioridade (consolidando a seção 1 e as marcações "Sim" da seção 7):

1. **D-01/D-03/D-06** — directory traversal na transferência de arquivos. **O bloqueador mais grave.**
2. **D-02** — leitura fora dos limites com vazamento de memória para disco/UI.
3. **R3** — teto único para todo tamanho/contador vindo do fio (A-07, A-08, A-10, B-04, B-07, D-05).
4. **C-02/C-05** — padrões de clipboard nas duas direções.
5. **C-03** — bind do modo de escuta e aviso de sessão não autenticada.
6. **D-08** — interruptor de transferência de arquivos no viewer (hoje o servidor decide).
7. **A-02** — aviso de sessão sem autenticação quando o servidor impõe `None`.
8. **C-04** — divisão por zero com formato de pixel inválido.
9. **A-06** — leitura fora dos limites ao decodificar senha salva.
10. **A-04** — teto de `DesktopSize` mais apertado (reduzir de ~2 GiB para algo como 16384×16384).
11. **R6 (E-01/E-02)** — verificação de hash do `fsvncupdater.exe` antes de executar/elevar, **antes**
    que exista qualquer instalação em diretório protegido.
12. **F-03/F-05** — `/guard:cf` no build de shipping real e fim da divergência entre os dois sistemas
    de build (sem isso, qualquer endurecimento futuro corre o risco de não chegar ao binário entregue).

Itens que **não** bloqueiam a versão estável, mas são fortemente recomendados antes dela: C-01, C-06,
C-09, A-05, A-09, B-04 (dentro de R3, já contado), B-05, F-01, F-02, F-04, F-06, F-07, F-08, F-09, E-07.

Itens que são limitação de protocolo e **não são bloqueadores técnicos** do produto: A-01 (cifragem
real), A-03 (DES/8 caracteres) — ver seção 9.

---

## 14. Conclusão técnica fundamentada

O FarSightVNC, no estado atual (`v1.1.0a`, HEAD `50557e9`), **não deve ser descrito como seguro**. Ele
é, em quase todo o núcleo RFB e nos decodificadores, **tão seguro ou mais seguro** que o TightVNC
Viewer 2.8.90 de onde nasceu — três falhas de corrupção de memória do upstream já foram corrigidas, as
dependências estão atualizadas e sem CVE aplicável, e o atualizador tem uma cadeia criptográfica de
qualidade real. Mas o produto herdou, sem nenhuma correção, a única falha **crítica** deste conjunto
(directory traversal na transferência de arquivos), e o redesign de UI do fork não aproveitou a
oportunidade de mudar os padrões de clipboard e de rebaixamento silencioso de autenticação que vinham
do upstream.

Respostas objetivas às dez perguntas do escopo:

**(1) Há vulnerabilidades herdadas do TightVNC?** Sim, e é onde está a maior parte do risco real:
D-01/D-02 (transferência de arquivos, Crítica e Alta), A-01/A-02/A-03 (transporte e autenticação,
estrutural do protocolo), C-02/C-05 (clipboard automático), R3 inteiro (seis pontos de alocação sem
teto). Nenhuma dessas foi introduzida pelo fork — todas já existiam em `8fd86d7`.

**(2) As modificações do fork introduziram novos riscos?** Sim, mas de magnitude menor que os
herdados: a fila de entrada sem teto (A-11/C-12), o `delete` sobre ponteiro controlado em
`WM_USER_RECONNECT`/`WM_USER_HOME_CONNECT` (C-01, rebaixado por G a Baixa-Média), o hook de teclado
engolindo `Win+L` (C-06), e a assimetria de endurecimento entre viewer e updater (F-01/F-08/F-09). O
atualizador inteiro é código novo do fork, e nele estão os riscos E-01 a E-10 — nenhum crítico no
layout de distribuição atual, mas E-07 (chave única sem revogação) é risco operacional severo.

**(3) Quais proteções o FarSightVNC oferece que o TightVNC não oferece?** zlib atualizado para a
release corrente (1.3.2, incorporando os achados da auditoria da 7ASecurity); três correções reais de
corrupção de memória nos decodificadores (ZRLE CPIXEL, `fillRect`, `setColor`); truncamento seguro do
nome de desktop no `ServerInit`; validações de geometria e de formato de pixel adicionadas no núcleo;
um canal de atualização com assinatura ECDSA própria, algo que o TightVNC simplesmente não tem; hook
de teclado com dono explícito e verificação de posse, mais robusto que o upstream; toolchain
modernizado (v145/SDK 10), que torna `/guard:cf` e `/CETCOMPAT` tecnicamente possíveis (ainda
desligados).

**(4) Um servidor comprometido pode explorar o viewer?** Sim, e o caminho mais forte não é corrupção
de memória — é D-01: uma descarga aprovada pelo operador grava arquivo arbitrário fora da pasta
escolhida, incluindo caminhos de autostart, sem qualquer diálogo adicional.

**(5) O viewer pode ser vetor de entrada numa máquina corporativa?** Sim, por D-01 (persistência via
autostart) e por C-02 (exfiltração contínua de clipboard corporativo). Ambos exigem apenas que o
operador conecte uma vez a um servidor hostil ou comprometido — não exigem MITM nem privilégio
adicional.

**(6) Quais riscos dependem só da infraestrutura?** A cifragem real do transporte (A-01) — só
resolvível por túnel SSH/VPN ou gateway TLS externo, já que o TightVNC Server não oferece VeNCrypt; a
limitação de 8 caracteres/DES da VNC Authentication (A-03), que é desenho do protocolo; o risco
"Crítico operacional" de E-07 (comprometimento da máquina de assinatura do dono), que nenhuma correção
de código resolve — só processo (HSM, chave de reserva offline).

**(7) Quais vulnerabilidades podem ser corrigidas sem tocar no servidor?** A enorme maioria: D-01/D-02
(FT), C-02/C-05 (clipboard), C-03 (modo de escuta), C-04 (divisão por zero), A-02 (aviso de `None`),
R3/R5/R7 inteiros, E-01/E-02/E-03 (updater). Todas as 18 recomendações da seção 10 são classificadas
como "só-no-viewer" ou "processo/updater", exceto a cifragem real (que depende de infraestrutura ou de
suporte VeNCrypt no servidor, item 5 da seção 10).

**(8) O mecanismo de atualização é suficientemente seguro?** A criptografia, sim: assinatura ECDSA
P-256 verificada antes do parse, hash em quatro pontos, HTTPS obrigatório, zip-slip impossível por
construção, nenhum bypass de verificação identificado, parser JSON manual sem falha de memória
encontrada (E-11). **Não**, quanto à execução: o binário que de fato instala e eleva é lançado de uma
pasta gravável pelo usuário sem nenhuma verificação de hash própria (E-01/E-02), e isso só não é o elo
mais fraco hoje porque o layout de distribuição inteiro já é uma pasta gravável — no dia em que houver
instalação em `Program Files`, essa lacuna se torna o caminho mais barato de elevação de privilégio.

**(9) O que precisa ser corrigido antes do beta?** A lista da seção 12, com destaque para D-01/D-02,
C-04, R3, o aviso de sessão não autenticada, R5 e a resolução da divergência de build (F-05) antes de
qualquer novo endurecimento ser adicionado só ao CMake.

**(10) Quais condições antes do primeiro release estável?** Todos os doze itens da seção 13,
especialmente D-01/D-03/D-06 (bloqueador absoluto), D-02, R3, os padrões de clipboard, o aviso de
`None`, e a verificação de hash do updater **antes** que exista instalação em diretório protegido —
corrigir essa lacuna depois de lançar o instalador é tarde demais, porque é exatamente a mudança de
contexto que faz a severidade subir de Baixa para Média-Alta.

---

### O que esta auditoria NÃO cobriu (lacunas explícitas de G)

Honestidade exigida pelo próprio processo de revisão cruzada — nenhum destes pontos foi auditado por
nenhum dos sete relatórios:

1. `tvnviewer/DesktopWindow.cpp` (recorte/escala do `WM_PAINT` sobre o DIB local) e
   `tvnviewer/ScaleManager.*` na aritmética linha a linha — B declarou fora de escopo; C confirmou as
   correções de alto nível (divisão por zero, overflow de `int`) sem auditar a aritmética de escala.
2. `region/Region.cpp` — B auditou só `Rect.h`/`Dimension.h`; `Region` agrega retângulos vindos do
   servidor e nunca foi lida linha a linha.
3. O parser de `config-lib` (`RegistrySettingsManager`, `IniFileSettingsManager`, `ConnectionHistory`)
   — ninguém auditou o parser das configurações persistidas, que vem de arquivo/registro editável por
   outro processo do usuário; o mesmo padrão de A-06 (índice fixo sobre string não verificada) pode
   existir lá.
4. `gui/` (`DibFrameBuffer`, diálogos) e `tvnviewer/shell/` — código **novo** do fork, onde
   regressões costumam aparecer; só C-03 o cita de passagem.
5. O mutex `Local\FarSightVNC-Updater-<hash>` do updater pode ser criado por qualquer processo do
   mesmo usuário para desarmar silenciosamente toda atualização subsequente — complemento de E-06
   que nenhum relatório registrou como achado próprio.
6. Associação de arquivo `.vnc` / abertura por duplo clique — ninguém verificou se o produto registra
   a extensão nem se há um fluxo de abertura que torne A-06 um vetor de um único clique.
7. Resolução de nomes, IPv6 e suporte a proxy/SOCKS — `SocketAddressIPv4::resolve` nunca foi
   examinado.
8. Se a reconexão assistida recria `RemoteViewerCore`/`DecoderStore` — decide se o estado zlib
   residual de B-10 e o `FrameBuffer` corrompido de A-04 sobrevivem à reconexão. Pergunta barata de
   responder, que nenhum relatório respondeu.
9. Caminho, permissões, rotação e DACL do arquivo de log — A-10 e C-08 mencionam o que vai para o log
   de passagem, mas nenhum relatório verificou onde o arquivo fica nem com qual DACL.
10. Se o processo de release (`tools/build.ps1`, `CLAUDE.md`) executa qualquer gate de regressão de
    segurança hoje — B propõe transformar `tools/fuzz-test.ps1 -Regression` nisso; ninguém verificou
    se já está amarrado ao shipping.

Esta auditoria é, portanto, **abrangente dentro do que cobriu**, mas não é exaustiva do produto. As
dez lacunas acima deveriam compor o escopo da próxima rodada.

