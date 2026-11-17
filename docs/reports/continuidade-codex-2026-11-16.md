# FarSightVNC — continuidade Claude → Codex → Claude

Checkpoint operacional iniciado em 2026-11-16. A versão candidata foi preparada como **v1.3.0a**, ainda sem publicação. Consultar o estado de validação abaixo antes de retomar.

**Estado ao encerrar:** implementação das duas frentes interrompidas concluída, build integrado e testes aprovados, instalador entregue apenas como fontes por ausência de compilador. Todas as alterações seguem no working tree; HEAD continua `fe135810b902ba7fde7a2ed35ad19586bb7c2ac8`. Retomar pelo aceite e pelos commits, sem repetir a investigação histórica.

## Contexto recuperado

- Repositório: `D:\DevelopmentLibrary\Apps\FarsightVNC`, branch `develop`, HEAD inicial `fe13581`.
- Versão inicial: `v1.2.1a`. Próxima versão planejada pelo Claude: `v1.3.0a` (funcionalidades novas).
- A sessão Claude `c174c696-85a3-4000-9e76-3e27d541fb78` parou por limite de uso. Havia alterações não commitadas de dois subagentes: liberação do teclado e testes de colagem de arquivos.
- O checkpoint Brain de 2026-11-14 estava defasado; o Git e a sessão local de 2026-11-16 foram usados para reconciliar o estado.

## Pedidos do dono recuperados

1. Remover worktrees pendentes (Claude já havia feito; na retomada havia somente a worktree principal, branches preservadas).
2. Colar arquivos locais no remoto com Ctrl+C/Ctrl+V.
3. Favoritos fixos na Home, sem acompanhar o scroll dos recentes.
4. Resolver captura de tecla local/remapeada antes que a ferramenta de screenshot consuma a tecla.
5. Última informação: o amigo usa **menos do teclado numérico**, teclado **Português ABNT2**, viewer **sem administrador e sem tela cheia**; o screenshot funciona quando o viewer perde o foco.
6. Criar instalador FarSightVNC. A regra vigente permanece **distribuir somente viewer**, sem instalar servidor/serviço TightVNC.
7. Usar subagentes suficientes, com economia de contexto, e deixar relatório completo para continuidade.

## Já commitado pelo Claude antes da retomada

- `94c8bd0`: favoritos fixos na Home.
- `baeac38`: captura de tecla local com hook de baixo nível no key-down.
- `4eb2ac6`, `6771aef`, `59d898b`, `15e37da`, `9137912`, `214a1e7`, `eb8ccb4`: protocolo, clipboard, lado servidor retido no código, viewer e testes iniciais de colagem.
- `fe13581`: regra explícita de compatibilidade TightVNC nos arquivos de instruções.

**Limite funcional importante:** colagem de arquivos exige capacidade opcional FSVC nas duas pontas. Contra TightVNC existente, continua dormente e Ctrl+V normal deve ser preservado. O código servidor não autoriza empacotar ou instalar servidor.

## Frentes desta retomada

- Subagente `clipboard_tests`: concluir testes de sessão/protocolo, revisar handshake, cancelamento e lifetime dos callbacks.
- Subagente `keyboard_toggle`: concluir botão/menu de liberar teclado, impedir envio também por WM_KEY/WM_CHAR, tornar Print Screen uma entrada local editável por padrão, testes pertinentes.
- Subagente `installer`: fontes e build de instalador viewer-only por usuário. Não foi encontrado compilador Inno/NSIS/WiX na inspeção inicial; não baixar ou instalar ferramenta implicitamente.
- Agente principal: integração, validação final, documentação e checkpoint.

## Validação e pendências

### Implementação concluída nesta retomada

**Clipboard e protocolo**

- Extraídos `ClipboardFilesHandshake` e `PasteKeystroke`, usados pelo viewer e pelo teste de sessão.
- Negociação exige as três capacidades FSVC: BEGIN, SET e REPLY. O código herdado verificava apenas as duas mensagens cliente→servidor.
- `onReply` agora publica os dados e sinaliza o evento sob o mesmo mutex. Antes, um timeout podia se intercalar antes da notificação e soltar uma espera posterior indevidamente.
- O listener de `ClipboardFilesCapability` é protegido por mutex; remover o listener espera callbacks em andamento, evitando acesso durante a destruição da janela/sessão.
- Como o protocolo não tem identificador de requisição, timeout/cancelamento durante o handshake impede nova colagem até reconectar. Isso evita aceitar resposta antiga como confirmação de uma operação nova. A janela/sessão nova recria o handshake.
- Corrigido o link MSBuild do teste com zlib/libjpeg e o parser do peer falso: `writeUTF8` inclui NUL no tamanho.
- Cenários cobrem peer TightVNC sem extensão, fornecedor TGHT incorreto, capacidades incompletas, ordem BEGIN→SET→Ctrl+V, Ctrl físico pressionado, rejeição, timeout, cancelamento, raízes inválidas e respostas inesperadas.
- **Limite:** `RemoteViewerCore` real comunica com pipes em memória; não há socket, upload real, Explorer, CF_HDROP remoto nem diálogo de progresso nesse teste.

**Teclado**

- Toggle por sessão exposto nas duas barras e menus, com indicador de estado. Não é salvo como preferência; uma sessão nova começa capturando o teclado.
- Libera teclas remotas antes de fechar a saída; limpa latches Ctrl/Alt. Bloqueia hook, WM_KEY*, WM_CHAR e comandos sintéticos durante liberação. Recaptura protege teclas locais ainda pressionadas até a soltura.
- `DesktopWindow::sendCtrlV` também verifica diretamente o estado de liberação antes de alterar o tradutor de teclas.
- Print Screen removido dos hardcodes de hook/tradutor. Entradas padrão `2C,2Ce` são editáveis e podem ser capturadas em Add key.
- Migração `LocalKeysPolicyVersion=1`: configurações legadas vazias ou com `6D` recebem Print Screen uma vez. O marker só é salvo após a lista. Um Clear posterior permanece vazio, permitindo Print Screen remoto.
- O E2E testa migração legada e, com `-RemotePrintScreen`, remoção explícita; restaura lista e marker anteriores no finally.

**Instalador e documentação**

- `tools/installer/FarSightVNC.iss`: Inno Setup por usuário em `%LOCALAPPDATA%\Programs\FarSightVNC`, x64, idiomas PT-BR/inglês, atalho no Iniciar e desktop opcional, sem servidor, serviços ou associação `.vnc`.
- `Build-Installer.ps1`: allowlist exata dos cinco arquivos, rejeita extras/diretórios/links/vazios, confere PE x64 e versões viewer/updater, extrai versão dos executáveis.
- Validação do payload aceita os binários reais; fixtures com servidor extra e PE inválido foram rejeitadas pelo subagente.
- **Bloqueio externo:** não há compilador Inno/NSIS/WiX instalado. Fontes e script estão prontos; Setup não foi compilado, instalado ou desinstalado. Nenhuma ferramenta foi baixada/instalada.
- Metadados da versão Inno em Aplicativos instalados permanecem do último Setup após update interno; limitação documentada.
- Viewer e updater preparados com versão numérica `1,3,0,0` e textual `1.3.0a`. Preservados UTF-16 LE/BOM do viewer e UTF-8 do updater.
- Atualizados README, instruções de versão, shipping PT/EN, MODIFICATIONS e notas candidatas. O instalador é asset opcional separado; zip/manifesto continuam com layout exigido.

### Validação integrada

Resultados finais observados pelo agente principal:

| Validação | Resultado |
|---|---|
| Build oficial MSBuild x64 Release: viewer, updater e cinco testes | Exit 0 |
| CMake configure/build x64 Release, todos os targets | Exit 0 |
| `ctest --preset x64-release --verbose` | 5/5, zero falhas, 22,59 s |
| Teste teclado do build oficial | 548 passaram, zero falhas |
| Fila de entrada do build oficial | 39 passaram, zero falhas |
| Favoritos do build oficial | 146 passaram, zero falhas |
| Clipboard do build oficial | 171 passaram, zero falhas |
| Atualizador do build oficial | 14 suites, 2384 checks, zero falhas |
| E2E teclado, migração legada/default local | 46 casos, zero falhas |
| E2E teclado, `-RemotePrintScreen` | 46 casos, zero falhas |
| `tools/release/Test-ReleaseScripts.ps1` | Todos os checks passaram; chave descartável, sem publicação |
| `Build-Installer.ps1 -ValidateOnly` no payload candidato v1.3.0a | Aprovado |
| `git -c core.whitespace=cr-at-eol diff --check` | Exit 0 |

O subagente também executou clipboard CMake Debug: 171/0. Não foi feito build Debug completo do viewer nesta retomada.

Os E2Es iniciam peer falso em loopback e usam PostMessage na janela do processo de teste; não injetam digitação no desktop real. A lista LocalKeys e o marker da migração são restaurados ao terminar. O caso local confirmou PrtSc local após migrar `6D` legado; o segundo confirmou XK_Print remoto quando removido explicitamente. Ambos confirmaram soltura de Ctrl, bloqueio de teclas e comandos sintéticos durante release, recaptura e isolamento da página Settings.

**Notas dos testes:** updater sem testes HTTP online, sem simulação de disco cheio e sem testar policy HKLM/admin. O teste de interop com manifesto PowerShell, inicialmente pulado pelo CTest sem variável de ambiente, foi exercitado separadamente por Test-ReleaseScripts. Nenhum resultado automatizado comprova Lightshot/ABNT2 físico, sessão TightVNC real ou colagem completa.

**Avisos de build, sem erro:** MSBuild emitiu LNK4006 (`__NULL_IMPORT_DESCRIPTOR` duplicado entre bibliotecas de importação) e LNK4020 em símbolos do `updater-lib.pdb` incremental existente. CMake emitiu MSB8028 em diretórios intermediários de quatro projetos. Não alterei a infraestrutura de build nem apaguei caches para tratar esses avisos. O executável foi gerado e os testes passaram; se precisar depurar/publicar com símbolos limpos, fazer rebuild isolado e investigar os avisos. Não declarar build sem warnings.

### Comandos para reproduzir

Executar da raiz do repositório, com acesso ao SDK/ambiente de desenvolvimento. Para o build oficial, a lista em `-Project` é uma string separada por ponto e vírgula:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "& .\tools\build.ps1 -Configurations Release -Platforms x64 -Project 'tvnviewer;farsight-updater;viewer-keyboard-test;viewer-input-queue-test;viewer-favorites-test;viewer-clipboard-files-test;updater-test'"
cmake --preset x64-release
cmake --build --preset x64-release --parallel 4
ctest --preset x64-release --verbose

powershell -NoProfile -ExecutionPolicy Bypass -File tools/keyboard-e2e-test.ps1 -Viewer build/bin/x64/Release/fsvncviewer.exe -Python C:\Users\Kellvyn\AppData\Local\Programs\Python\Python313\python.exe
powershell -NoProfile -ExecutionPolicy Bypass -File tools/keyboard-e2e-test.ps1 -Viewer build/bin/x64/Release/fsvncviewer.exe -Python C:\Users\Kellvyn\AppData\Local\Programs\Python\Python313\python.exe -RemotePrintScreen
powershell -NoProfile -ExecutionPolicy Bypass -File tools/release/Test-ReleaseScripts.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/installer/Build-Installer.ps1 -ShippingDirectory build/shipping/FarSightVNC-v1.3.0a -ValidateOnly
```

Python estava invisível no sandbox, mas existe no caminho acima fora dele. O launcher `py -0p` também só listou as instalações no contexto com acesso ao perfil.

### Artefatos locais e evidência

- Binários oficiais: `build/bin/x64/Release/fsvncviewer.exe` e `fsvncupdater.exe`, FileVersion/ProductVersion `1.3.0a`.
- Candidato portátil: `build/shipping/FarSightVNC-v1.3.0a/` e `build/shipping/FarSightVNC-v1.3.0a.zip`.
- Zip plano, exatamente cinco entradas: `fsvncviewer.exe`, `fsvncupdater.exe`, `LEIAME.txt`, `LICENSE.txt`, `README.txt`. Sem servidor, Setup ou subpastas.
- SHA-256 do zip candidato: `856E91582F78F8399262CA8941EA1A2273E3E82C9D1403E8EED5BC49FED018FB`.
- Esse candidato **não é release assinado/publicado**. Não há manifesto de produção ou assinatura gerados para ele nesta retomada.
- Logs locais ignorados pelo Git: `build-logs/codex-msbuild-release.log`, `codex-cmake-configure-release.log`, `codex-cmake-build-release.log`, `codex-ctest-release.log`, `codex-msbuild-<nome-do-teste>.log`, `codex-keyboard-e2e-local.log`, `codex-keyboard-e2e-remote-print.log`, `codex-release-scripts.log`.
- `build/` e `build-logs/` são ignorados: não serão recuperados somente pelo Git. Fontes e este relatório precisam dos commits de continuidade listados abaixo.

### Continuidade obrigatória antes de publicar

1. Fazer aceite físico no computador do amigo: Add key com menos numérico ABNT2, screenshot em janela normal sem admin, toggle local e volta ao remoto, Ctrl/Alt sem ficar presos.
2. Conferir visualmente barras comum/fullscreen, indicador de teclado e janelas estreitas, temas e DPI.
3. Testar servidor TightVNC existente sem alterações; confirmar teclado, texto/clipboard e transferências anteriores.
4. Testar colagem de arquivos completa com ambas as pontas FarSightVNC em ambiente descartável, incluindo pastas, cancelamento e reconexão. O shipping permanece viewer-only.
5. Com Inno Setup disponível, compilar e testar instalar/reinstalar/desinstalar em VM/conta descartável conforme `docs/installer/README.md`. Não testar instalação destrutiva na conta real do dono.
6. Revisar e criar commits atômicos; as alterações desta retomada permanecem não commitadas. Separar clipboard, teclado, instalador e preparação da versão/documentação; `DesktopWindow.cpp` e `ViewerWindow.cpp` exigem seleção de hunks porque têm mudanças de duas frentes.
7. Finalizar notas candidatas, montar shipping no Desktop e zip plano apenas com viewer/updater/LEIAME/LICENSE/README. Substituir versão anterior somente no procedimento de entrega autorizado.
8. Gerar manifesto e assinatura com `New-UpdateManifest.ps1` a partir do zip exato. Publicar os três assets obrigatórios; Setup só como quarto asset após validado. Rodar `Test-ReleaseAssets.ps1 -Tag v1.3.0a` após upload.

Não houve commit, push, tag, publicação, empacotamento no Desktop nem instalação de servidor nesta retomada. Não confundir fonte de instalador com instalador entregue.

## Restrições operacionais observadas

- PowerShell bloqueia scripts pela política padrão; execução pontual com `-ExecutionPolicy Bypass` funcionou, sem mudar a política da máquina.
- Build no sandbox falhou ao ler `AppData\Local\Microsoft SDKs`; escalada pontual do build foi aprovada automaticamente para o subagente.
- Brain confirmado em `C:\Users\Kellvyn\Documents\Laura Virtual Assistant\Brain`, identidade README LAURA Brain. Bridge suporta `Status`, `Search -IncludeDraft`, `Read`, `New -Agent codex` (draft); não há update/tagging suportado.
- Não confundir o pedido histórico do instalador com autorização para alterar a instalação TightVNC existente.
- Checkpoint persistido pelo writer nativo `New -Agent codex` como **draft** em `Inbox/farsightvnc-checkpoint-v1-3-0a-candidato-2026-11-16-20a85c5a.md`. Verificado por `Read`, existência do arquivo e comparação do corpo completo (3110 caracteres), sem truncamento. Não foi promovido a ativo; não houve edição manual de YAML, migração ou alteração das notas antigas. Buscas específicas com IncludeDraft não encontraram checkpoint equivalente desta versão antes da escrita.
