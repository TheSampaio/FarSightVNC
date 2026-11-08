# Investigação do HANG intermitente no fuzz `desktopsize` seed 25 (T8)

- Base: `0d8d429` (branch `TheSampaio/fsv-t8-hang`, a partir de `modernization/integration`). A investigação não alterou código; a correção aprovada pelo coordenador está na seção "Correção aplicada".
- Viewers comparados (ambos Debug x64): novo = build deste worktree (C++20); antigo = v0.1.1a, `fsv-t1-baseline\x64\Debug\tvnviewer.exe` (somente leitura).
- Ambiente: Windows 11 Pro 10.0.26200, 12 processadores lógicos, Python 3.13.

## Conclusão

**Não é deadlock entre locks nem lentidão: é um laço infinito (livelock) herdado do TightVNC 2.8.90.** Existe no v0.1.1a, no viewer C++20 e em Release.

`FrameBuffer::setColor()` (`rfb/FrameBuffer.cpp`) nunca termina quando o frame buffer ainda não tem formato de pixel (`bitsPerPixel == 0`):

```cpp
int pixelSize = m_pixelFormat.bitsPerPixel / 8;          // 0
UINT8 *endPixPtr = (UINT8 *)m_buffer + getBufferSize();  // nullptr + 0
UINT8 *pixPtr = (UINT8 *)m_buffer;                       // nullptr
for (; pixPtr <= endPixPtr - pixelSize; pixPtr += pixelSize)   // 0 <= 0, e pixPtr += 0
  memcpy(pixPtr, &color, pixelSize);                     // memcpy(nullptr, ..., 0)
```

O laço é o `FrameBuffer.cpp` do import original (`git blame`: `8fd86d7`, "Import TightVNC 2.8.90"). O chamador também é original: o ramo "dimensão vazia" de `DesktopWindow::setNewFramebuffer()` (`DesktopWindow.cpp:625`) chama `m_framebuffer.setColor(0,0,0)`. Isso acontece se a **primeira** mudança de tamanho que o `FbUpdateNotifier` entrega à janela for 0x0, antes de qualquer tamanho não vazio ter criado a DIB.

### Por que o fuzz trava só às vezes (corrida)

1. O `ServerInit` (320x240 na seed 25) chama `onPropertiesFb()`, que só marca `m_isNewSize` (flag, sem fila).
2. A mensagem 0 da seed 25 começa com `desktopsize 0x0/zero` e é processada pela thread do core, que atualiza o frame buffer do core para 0x0.
3. A thread `FbUpdateNotifier` lê a dimensão do frame buffer do core **quando acorda**. Se acordar depois do passo 2 (CPU disputada, thread escalonada tarde), vê 0x0 e chama `setNewFramebuffer(0x0)` com `m_framebuffer` nunca configurado. Se acordar antes, cria a DIB 320x240 e tudo segue normal (e os 0x0 seguintes são inofensivos, porque a DIB já tem formato).

Isso explica a taxa baixa e o aparecimento sob carga ou em execução paralela.

### Quem segura o quê (dump de hang, v0.1.1a e viewer novo, mesma pilha)

| Thread | Estado | Detalhe |
|---|---|---|
| `FbUpdateNotifier::execute` | **Running, ~4,4 G ciclos/s (1 núcleo a 100 %)** | `memcpy` <- `FrameBuffer::setColor` (FrameBuffer.cpp:77/78) <- `DibFrameBuffer::setColor` <- `DesktopWindow::setNewFramebuffer` (DesktopWindow.cpp:619/625) <- `ViewerWindow::onFrameBufferPropChange` <- `FbUpdateNotifier::execute` (FbUpdateNotifier.cpp:107). Segura `m_fbLock` (FbUpdateNotifier.cpp:106) e `DesktopWindow::m_bufferLock` (DesktopWindow.cpp:604). |
| Thread da UI | Blocked, 0 ciclos | `LocalMutex::lock` <- `DesktopWindow::onPaint` (DesktopWindow.cpp:89, `m_bufferLock`). A janela fica "Não está respondendo". |
| `RemoteViewerCore::execute` | Blocked | `LocalMutex::lock` <- `processPseudoEncoding` (RemoteViewerCore.cpp:1135, `m_fbLock`). Por isso o viewer deixa de ler o socket. |

- Wait Chain Traversal (WCT) do Windows: `cycle=no`. Não há ciclo de espera. Toda cadeia termina na thread do notifier, que está **Running**.
- Locais da pilha lidos no processo vivo (v0.1.1a): `pixelSize = 0`, `sizeInPixels = 0`, `pixPtr = 0`. No dump (memória do `FrameBuffer` lida na dump, sem depurador), os bytes de dimensão, `PixelFormat` e `m_buffer` estão zerados; só há ponteiros de vtable.
- Duas amostras com 3 s de intervalo mostram a mesma instrução no laço. Após o dump, a janela continuou travada por mais 10 s (somados aos 20 s de timeout e aos 3 s da sondagem, mais de 33 s). Ao matar o processo, nada se recupera sozinho.

## Taxa de HANG medida (caso `desktopsize` seed 25, 1 caso = 1 viewer iniciado)

Comando-base: cópia de `tools/fuzz-test.ps1` em scratchpad, só com a faixa de portas trocada para 5960-5999 (porta base 5970 ou 5980), timeout de 20 s, `-Seeds 25` repetida, alternando blocos antigo/novo. A cópia também chama um utilitário próprio (`hangstack.exe`) antes do dump. Carga: 24 ou 36 processos `python -c "while True: pass"` com prioridade normal (12 núcleos lógicos).

| Cenário | v0.1.1a | viewer novo (C++20) |
|---|---|---|
| Sem carga, `-Parallel 1` | 0/100 | 0/100 |
| 24 processos de carga, `-Parallel 1` | 0/100 | 0/100 |
| 36 processos de carga, `-Parallel 3`, rodada 1 | 4/90 | 3/90 |
| idem, rodada 2 | 3/90 | 7/90 |
| idem, rodada 3 | 4/90 | 0/90 |
| **Total com `-Parallel 3` + carga** | **11/270 (4,1 %)** | **10/270 (3,7 %)** |
| `ServerInit` 0x0 (reprodução determinística, abaixo) | trava | trava (Debug e Release) |

Os viewers não diferem de forma mensurável. O C++20 não introduziu o problema. O fuzz completo de 350/350 em v0.1.1a só prova que a corrida não foi perdida naquela execução.

### Reprodução determinística (sem corrida)

Um servidor RFB falso cujo `ServerInit` declara frame buffer **0x0** trava o viewer 100 % das vezes, porque a primeira mudança de tamanho já é vazia:

```powershell
# servidor: cópia de tools/fuzz-rfb-server.py com uma opção --fb WxH (scratchpad)
python -I fuzz-rfb-server-scratch.py --port 5981 --scenario empty-update --fb 0x0 --messages 3 --linger 60 --duration 90
tvnviewer.exe -host=127.0.0.1 -port=5981 -viewonly=yes -encoding=raw -useclipboard=no
```

| Viewer | `--fb 0x0` | Controle `--fb 320x240` |
|---|---|---|
| v0.1.1a Debug x64 | travado (`Responding=False`, thread a ~3,8 G ciclos/s em `FrameBuffer::setColor`) | responde |
| Novo Debug x64 | travado, mesma pilha | n/a |
| Novo **Release** x64 | travado, 1,98 s de CPU em 2 s | n/a |

Com o script do repositório e `-DumpOnHang`, um wrapper `-Python` que injeta `--fb 0x0` também produz o HANG com dump (ver "Ferramenta").

## O timeout de 20 s é adequado?

Sim. O travamento é permanente, e não lentidão: a mesma pilha com a thread a 100 % de CPU em amostras separadas, a janela não volta em mais de 33 s, e a instrução do laço não muda. Aumentar o timeout só atrasaria o diagnóstico. A detecção `SendMessageTimeout(WM_NULL, 3000 ms)` após o timeout funciona.

## Correção proposta (pequena)

1. **`rfb/FrameBuffer.cpp` — `FrameBuffer::setColor`**: sair cedo quando não há o que preencher:

   ```cpp
   int pixelSize = m_pixelFormat.bitsPerPixel / 8;
   if (m_buffer == 0 || pixelSize <= 0 || getBufferSize() < pixelSize) {
     return;
   }
   ```

   Cobre também `m_buffer == nullptr` com `pixelSize > 0`, em que `endPixPtr - pixelSize` dá a volta e o `memcpy` iria a um ponteiro nulo (AV). Não muda o comportamento para frame buffers válidos, nem para os de tamanho 0x0 com formato definido (o laço já não executa).
2. **Teste de regressão**: cenário `serverinit-0x0` em `tools/fuzz-rfb-server.py` (ServerInit 0x0) mais um caso em `$regressionCases` de `tools/fuzz-test.ps1`.
3. Efeito esperado após a correção: com tamanho 0x0 o viewer mostra a janela vazia (ramo "preencher o viewer" de `setNewFramebuffer` + `repaint`) e continua responsivo.

Compatibilidade RFB: a correção só protege o preenchimento local do frame buffer. O protocolo não muda e servidores TightVNC sem modificação não são afetados.

## Correção aplicada e verificação

Aplicadas as três mudanças acima: guarda em `FrameBuffer::setColor` (antes de qualquer aritmética de ponteiro), cenário `serverinit-0x0` (ServerInit 0x0, depois `DesktopSize` 64x48 e dois quadros Raw: vermelho, azul) e o caso em `$regressionCases`. Versão do produto não alterada (o coordenador sobe na integração).

Auditoria do mesmo ramo: os demais laços que avançam por pixel (`fillRect`, `FrameBuffer.cpp:100` e `:393`) são limitados por contador (`x < right`, `iRow < height`) e `fillRect` já sai cedo com retângulo vazio. Não há outro laço com `pixelSize == 0` fora de `setColor`.

| Verificação | Resultado |
|---|---|
| Servidor `serverinit-0x0` contra v0.1.1a Debug x64 | trava (`Responding=False`, 2,0 s de CPU em 2 s) |
| idem, viewer corrigido Debug x64 | responde, 0 s de CPU, centro da janela azul (RGB 0,0,255): continua desenhando após o `DesktopSize` válido; servidor `sent=25/25 end=completed` |
| idem, viewer corrigido Release x64 | idêntico (responde, 0 s de CPU, centro azul, 25/25) |
| `fuzz-test.ps1 -Regression` (10 casos, viewer corrigido) | 10 OK, 0 HANG/CRASH, saída 0 |
| `-Regression` com o viewer v0.1.1a | `raw#serverinit-0x0` HANG, saída 1 (o caso pega o defeito) |
| Seed 25 `desktopsize`, `-Parallel 3`, 36 processos de carga, viewer corrigido Debug x64 | **0 HANG em 360 execuções** (antes: 10/270 no viewer novo, 11/270 no antigo) |
| Fuzz completo (`-Parallel 3`, seeds 1 a 50, 7 codificações) | 350 OK, 0 HANG, 0 CRASH, 0 ERROR, saída 0 |
| Testes de unidade Release x64 | `viewer-keyboard-test` 227/0, `viewer-input-queue-test` 39/0, `viewer-favorites-test` 88/0, todos saída 0 |

Observação: na rodada de 360 execuções os dois "lados" do alternador apontaram para o mesmo viewer corrigido (não houve controle antigo nessa rodada; o controle é a taxa de 4 % medida antes, nas mesmas condições). A rodada usou cópia de `fuzz-test.ps1` com portas 5960-5999.

## Ferramenta adicionada: `fuzz-test.ps1 -DumpOnHang <pasta>`

- Ao classificar um HANG, grava um minidump de memória completa (`MiniDumpWriteDump` de `dbghelp.dll`, flags `WithFullMemory | WithHandleData | WithUnloadedModules | WithFullMemoryInfo | WithThreadInfo`) **antes** de matar o viewer. Depois sonda a janela por até 10 s e acrescenta ao detalhe `dump=<arquivo>; recovered after N s` ou `still hung after 10 s`.
- Verificado com a versão commitada: HANG forçado (wrapper `-Python` com `--fb 0x0`) gerou `hang-desktopsize-25-pid<pid>-<data>.dmp` de 79,8 MB, relido com um parser de minidump (400 intervalos de memória, 79,7 MB) e saída 1. `-Regression` sem `-DumpOnHang`: 9 casos, 0 HANG/CRASH, saída 0.
- Para o hang não ser silencioso, a mensagem do HANG deixa de ser truncada em 160 caracteres quando `-DumpOnHang` está presente.

## Limitações e o que não foi verificado

- cdb, WinDbg e procdump **não estão instalados** nesta máquina. As pilhas vieram de um utilitário próprio (`hangstack.cpp`: dbghelp `StackWalk64` + WCT + leitura de locais com PDB), executado no processo vivo antes do dump. O dump foi lido apenas com um parser de minidump (leitura de memória), não aberto em depurador.
- A explicação da corrida (passos 1 a 3) é inferência a partir do código e do estado zerado do `FrameBuffer` no dump. Não foi provado, por um log de ordem de eventos, que o notifier acordou depois da mensagem 0 em cada hang, e a ordem não foi instrumentada porque o código de produto não foi alterado.
- A taxa de HANG depende da máquina e da carga: com carga leve ou `-Parallel 1`, 0 em 200 execuções por viewer.
- Um servidor TightVNC real não costuma enviar 0x0 no `ServerInit`; o risco prático fora do fuzz é um servidor sem tela (sessão trocando de resolução) cujo primeiro tamanho seja 0x0. Não foi testado contra um servidor real.
- Release foi verificado apenas no cenário determinístico `--fb 0x0`, não na seed 25.
- Dumps, pilhas e utilitários ficam fora do git, no scratchpad da sessão:
  `C:\Users\Kellvyn\AppData\Local\Temp\claude\9f57891788c3540d\76807489-5ef6-42d0-9b3b-4eba209cedc4\scratchpad\dumps\` (`p3c-old\` = v0.1.1a com locais, `p3b-new\` = viewer novo, `committed-script\` = dump gerado pelo script commitado), `hangstack.cpp`, `run-matrix.ps1`, `fuzz-rfb-server-scratch.py`, `dumpmem.py`.
