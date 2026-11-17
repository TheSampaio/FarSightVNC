# FarSightVNC — instruções para agentes

## Compatibilidade com a infraestrutura TightVNC (obrigatório)

- **O FarSightVNC substitui o TightVNC na infraestrutura existente do dono, sem mudança nenhuma do outro lado.** O viewer tem de conectar e funcionar contra os servidores TightVNC já instalados: protocolo RFB, tipos de segurança, autenticação, codificações, arquivos `.vnc`, chaves de registro `HKCU\Software\TightVNC\Viewer` e opções de linha de comando continuam compatíveis. Essa regra vence conveniência, desempenho e funcionalidade nova.
- Extensão de protocolo é permitida, mas **só como capacidade negociada e opcional**, nunca como requisito. O servidor anuncia, o viewer habilita apenas o que foi anunciado (`RemoteViewerCore::readCapabilities`), e quando a capacidade está ausente o comportamento tem de ser **idêntico ao anterior**, sem erro, sem aviso e sem caminho degradado visível.
- Toda extensão nova usa a assinatura de fornecedor própria `VendorDefs::FARSIGHT` (`"FSVC"`), nunca `"TGHT"` nem `"STDV"`: `CapsContainer::enable` casa código, fornecedor e nome juntos, e um fornecedor privado impede que um par TightVNC case os mesmos códigos por acidente, hoje ou se o TightVNC passar a usá-los. Códigos novos ficam na faixa `0xFC0004xx` em diante.
- Funcionalidade que exige as duas pontas FarSightVNC (ex.: colagem de arquivos pelo clipboard) é **dormente** enquanto o remoto for TightVNC. Isso é aceitável; quebrar a conexão com TightVNC não é.
- **O shipping contém somente o viewer.** Não empacote servidor: ele disputaria o nome de serviço, a porta 5900 e a chave `HKLM\SOFTWARE\TightVNC\Server` com o TightVNC instalado. Mudar isso exige decisão explícita do dono.

## Versionamento (obrigatório)

- Formato: `vMAJOR.MINOR.FIXs`, onde o sufixo `s` é `a` (alpha), `b` (beta) ou nenhum (estável). Ex.: `v1.2.5a`.
- **Toda mudança entregue (build enviado ao dono / shipping) incrementa a versão**: correção → FIX, funcionalidade → MINOR, quebra de compatibilidade → MAJOR (zera os campos à direita).
- A versão fica em `tvnviewer/tvnviewer.rc` (UTF-16 LE): `FILEVERSION`/`PRODUCTVERSION` numéricos `MAJOR,MINOR,FIX,0` e as strings `FileVersion`/`ProductVersion` com a versão completa (ex.: `0.1.0a`). O texto "Based on TightVNC Viewer 2.8.90" continua indicando a base.
- Registre a versão na mensagem de commit, no `MODIFICATIONS.md` e no `LEIAME.txt` (português) e no `README.txt` (inglês) do shipping, que acompanham os executáveis e o `LICENSE.txt`.
- Shipping na área de trabalho do dono: `Desktop\FarSightVNC-v<versão>\` + `.zip` (substitui a pasta/zip anterior), com apenas `fsvncviewer.exe`, `fsvncupdater.exe`, `LEIAME.txt`, `LICENSE.txt` e `README.txt` na raiz (layout plano; só 64 bits, sem versão 32 bits).
- Toda versão publicada no GitHub DEVE anexar, além do `.zip`, os assets `farsightvnc-update.manifest` e `farsightvnc-update.manifest.sig` gerados por `tools/release/New-UpdateManifest.ps1` a partir do zip exato que foi publicado; sem eles o atualizador do app não instala a versão e só oferece a página de releases. Depois do upload, `tools/release/Test-ReleaseAssets.ps1 -Tag v<versão>` tem de passar (procedimento em `docs/updater/RELEASING.md`).
- Versão atual: `v1.2.1a`.

## Commits (obrigatório)

- **Commits atômicos**: cada commit contém uma única mudança lógica e coesa (uma correção, uma funcionalidade, um refactor, uma atualização de docs ou um bump de versão), que compila e passa nos testes sozinha. Não misture mudanças não relacionadas no mesmo commit; divida o trabalho em vários commits quando necessário.
- Mensagens no padrão Conventional Commits (`tipo(escopo): resumo`), com corpo explicando o quê e o porquê quando não for óbvio.
- Mudanças do dono que estejam no working tree vão em commits próprios, separados do trabalho dos agentes.
