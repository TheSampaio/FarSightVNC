# FarsightVNC — Relatório da Fase 0 (Fundação)

- **Data:** 2026-10-29
- **Executado por:** Claude (agente único; a fase é sequencial e não justificava subagentes)
- **Branch de trabalho:** `develop` (a `main` contém só o código original)
- **Máquina:** Windows 11 Pro 10.0.26200, usuário sem privilégios de administrador (sessão não elevada)

## 1. Resumo

| Item | Resultado |
|---|---|
| Git com baseline intacto | Feito: commit `8fd86d7` na `main` com o TightVNC 2.8.90 sem alterações |
| Build reproduzível | Feito: x64/Win32 × Release/Debug compilam com MSVC v145 + SDK 10 (`tools/build.ps1`) |
| Paridade com o binário oficial | Verificada no teste de fumaça e na medição de recursos (seção 6) |
| Push para o GitHub | **Pendente:** a criação do repositório remoto foi bloqueada pela política de permissões do Claude Code (seção 3) |
| Alterações no Windows | Nenhuma instalação no sistema, nenhum serviço, nenhuma regra de firewall, nenhum privilégio de administrador (seção 4) |

## 2. Commits (branch `develop`)

| Commit | Descrição |
|---|---|
| `8fd86d7` | Import do código original TightVNC 2.8.90 (baseline, também é a `main`) |
| `ff7ab9e` | `.gitignore` para saídas de build do Visual Studio |
| `962e75e` | Projetos migrados para MSVC v145 e Windows SDK 10 (só `PlatformToolset` e `WindowsTargetPlatformVersion`, 35 `.vcxproj`) |
| `81261b3` | `#include <string>` que faltava em `rfb/RfbKeySym.h` (sem mudança de comportamento) |
| `356df0f` | Ignorar arquivos gerados pelo compilador de mensagens (`win-event-log`) |
| `332db87` | `tools/build.ps1`: build reproduzível via vswhere/MSBuild |
| `f7ae9b0` | `tools/smoke-test.ps1`: teste de fumaça de conexão |
| `2f8d2b0` | `tools/measure-baseline.ps1`: medição de recursos |
| `2fc3c3e` | `tools/test-server.ps1`: servidor de teste local, somente loopback |
| `b2cb695` | Scripts de teste não deixam rastro no histórico do Viewer |
| (seguintes) | `MODIFICATIONS.md` (aviso de modificações exigido pela GPL) e este relatório |

Licença: os avisos GPLv2-ou-posterior originais foram mantidos. O arquivo alterado recebeu um aviso "Modified for FarsightVNC" com a data, e `MODIFICATIONS.md` lista todas as mudanças (GPLv2 §2a).

## 3. GitHub

- **Conta autenticada no `gh`:** `TheSampaio` (escopos `repo`, `workflow`).
- **Tentativa:** `gh repo create TheSampaio/FarsightVNC --private --source . --remote origin`, seguida do push de `main` e `develop`.
- **Resultado:** **negado pela política de permissões do Claude Code** (classificada como envio de dados para serviço externo). Nada foi enviado e nenhum repositório foi criado.
- **Para concluir:** autorize essa ação ou crie o repositório você mesmo e me passe a URL. Depois, só faltam `git remote add origin <url>`, `git push -u origin main` e `git push -u origin develop`.
- **Visibilidade proposta:** **privado** por padrão. Pode ser tornado público depois; a GPL não obriga a publicar enquanto não houver distribuição de binários.

## 4. O que foi instalado, criado ou alterado na máquina

### 4.1 Instalações
**Nenhuma instalação no sistema.** O MSI oficial foi apenas *extraído* (`msiexec /a`, extração administrativa). Esse comando não registra o produto, não cria serviço, não grava em Program Files e não exige administrador.

### 4.2 Arquivos criados fora do repositório
`D:\DevelopmentLibrary\Apps\FarsightVNC-testenv\`

| Caminho | Conteúdo |
|---|---|
| `downloads\tightvnc-2.8.90-gpl-setup-64bit.msi` | MSI oficial baixado de `https://www.tightvnc.com/download/2.8.90/`. SHA-256 `9693CB4CDC2E7E482E8E3F5890415B57EF87DE3369FA5C337DB516218A1170CE`, assinatura Authenticode válida de "OOO GlavSoft" |
| `downloads\msi-extract.log` | Log da extração |
| `official-2.8.90\PFiles\TightVNC\` | Binários oficiais extraídos (`tvnviewer.exe`, `tvnserver.exe` etc.), todos com assinatura GlavSoft válida. Servem de referência (baseline) |
| `registry-backup\HKCU-TightVNC-before-tests.reg` | Backup das suas configurações do TightVNC Viewer (HKCU), feito antes dos testes |

Para remover tudo: apague a pasta `FarsightVNC-testenv`.

Dentro do repositório, as saídas de build (`x64\`, `Release\`, `Debug\`, pastas de cada projeto) e `build-logs\` são ignoradas pelo Git. Também foi criada (e já removida) uma pasta temporária `D:\DevelopmentLibrary\Apps\FarsightVNC-build-logs`.

### 4.3 Registro do Windows (apenas HKCU, o seu usuário)

| Chave | Ação | Estado final |
|---|---|---|
| `HKCU\Software\TightVNC\Server` | Criada com a configuração de teste: porta 5910, somente loopback, sem HTTP, senha de teste | **Removida** ao final. É recriada sob demanda por `tools/test-server.ps1` |
| `HKCU\Software\TightVNC\Viewer\History\127.0.0.1::5910` | Criada automaticamente pelo Viewer durante os testes | **Removida.** Suas configurações do Viewer foram comparadas com o backup: 0 diferenças. Os scripts agora limpam isso sozinhos |

Nenhuma chave em `HKLM` foi alterada.

### 4.4 Firewall, serviços e rede
- **Firewall:** nenhuma regra criada ou alterada. O servidor de teste escuta só em `127.0.0.1:5910`, o que não aciona o firewall. Verifiquei que não existe nenhuma regra para `tvnserver`, `tvnviewer` ou FarsightVNC.
- **Serviços:** nenhum criado, alterado ou parado.
- **Processos:** o servidor de teste foi encerrado. Nenhum processo dos testes ficou rodando.

### 4.5 Instalação pré-existente (não foi tocada)
Já havia nesta máquina um **TightVNC Server 2.8.85 instalado como serviço** (`tvnserver`, início automático, `C:\Program Files\TightVNC`). Ele escuta em **todas as interfaces** na porta 5900, com a interface HTTP ativa na 5800, `AllowLoopback=0` e autenticação VNC ativa. Não alterei nada nele.

Observação de segurança, para sua avaliação: a autenticação VNC usa DES e não criptografa o tráfego. Se essa máquina estiver em rede não confiável, considere restringir o acesso por firewall, VPN ou túnel SSH e desativar o HTTP (5800) se não for usado.

## 5. Build

- **Ferramentas:** Visual Studio Community 2026 (MSBuild 18.x), toolset v145 (MSVC 14.51.36231), Windows SDK 10.0.26100.
- **Comando:** `.\tools\build.ps1 -Target Rebuild`. Rebuild completo do `tvnviewer` e dependências em Release|x64, Release|Win32, Debug|x64 e Debug|Win32: **0 erros**.
- **Avisos:** 40 avisos em Release|x64. Os mais comuns são C4996 (APIs obsoletas), C4312/C4302/C4311 (conversões de ponteiro em 64 bits) e C4244. São do código original e ficam registrados para a Fase 6.
- **Solução completa:** a `tightvnc2017.sln` inteira (incluindo o servidor) também compila em Release|x64.
- **Consequência da migração:** o Windows XP deixa de ser plataforma suportada (o toolset `v141_xp` foi substituído).

## 6. Testes executados

**Ambiente:** servidor **oficial** TightVNC 2.8.90 (`tvnserver.exe -run`, modo aplicativo) em `127.0.0.1:5910` com autenticação VNC.

Os viewers rodaram com `-viewonly=yes`, para que **nenhum** teclado ou mouse fosse injetado na sua área de trabalho real.

### 6.1 Teste de fumaça (`tools/smoke-test.ps1`)

| Viewer | tight | zrle | hextile | rre | senha errada rejeitada |
|---|---|---|---|---|---|
| Compilado Release x64 | OK | OK | OK | OK | OK |
| Compilado Release Win32 | OK | OK | OK | OK | OK |
| Compilado Debug x64 | OK | OK | OK | OK | OK |
| Oficial 2.8.90 x64 | OK | OK | OK | OK | OK |

Critério de "OK" para os encodings: o processo permanece vivo, há uma sessão TCP estabelecida e a janela da sessão abre com o nome do desktop remoto. Para a senha errada: não há sessão estabelecida e aparece a janela de erro.

### 6.2 Baseline de recursos (`tools/measure-baseline.ps1`)
Sessões de 30 s, 3 repetições, encoding tight, escala automática. Valores são a mediana.

| Viewer | CPU (% de 1 núcleo) | Pico de working set | Pico de memória privada | Handles | Objetos GDI | Objetos USER |
|---|---|---|---|---|---|---|
| Compilado x64 | 0,9 | 38,3 MB | 27,3 MB | 208 | 31 | 20 |
| Oficial 2.8.90 x64 | 0,9 | 38,3 MB | 27,4 MB | 208 | 31 | 20 |

**Limitações:**
- Medido com o desktop praticamente parado e com o viewer exibindo a própria máquina (loopback). Os números servem para **comparar builds**, não representam uso real.
- Latência, cenários 1080p/1440p/4K, monitores verticais, sessões longas e servidores de terceiros **não foram medidos nesta fase**. Entram com os harnesses das Fases 1 a 5.

## 7. Critérios de aceite da Fase 0

| Critério | Status |
|---|---|
| Baseline original versionado e intacto | Atendido |
| Build reproduzível por script | Atendido |
| Build atual equivalente ao binário oficial na matriz de testes disponível | Atendido (fumaça e recursos) |
| Ambiente de teste local sem impacto no sistema | Atendido |
| Push para o GitHub | **Pendente de autorização** |

## 8. Como reproduzir

```powershell
.\tools\build.ps1                                   # build de todas as configurações
.\tools\test-server.ps1 -Server <caminho>\tvnserver.exe -Password <senha de teste>
.\tools\smoke-test.ps1 -Viewer .\x64\Release\tvnviewer.exe -Password <senha de teste>
.\tools\measure-baseline.ps1 -Viewers @{ built = '.\x64\Release\tvnviewer.exe'; official = '<oficial>\tvnviewer.exe' } -Password <senha de teste>
.\tools\test-server.ps1 -Stop -RemoveConfig         # encerra e remove a configuração de teste
```

## 9. Próximos passos

1. **Autorizar** a criação do repositório no GitHub e o push (ver seção 3).
2. **Aprovar o início da Fase 1** (teclado):
   - harness com um servidor RFB falso que grava os keysyms, sem injetar nada na sua área de trabalho;
   - depois, as correções descritas no planejamento.
