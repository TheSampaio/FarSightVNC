# FarSightVNC — instruções para agentes

## Versionamento (obrigatório)

- Formato: `vMAJOR.MINOR.FIXs`, onde o sufixo `s` é `a` (alpha), `b` (beta) ou nenhum (estável). Ex.: `v1.2.5a`.
- **Toda mudança entregue (build enviado ao dono / shipping) incrementa a versão**: correção → FIX, funcionalidade → MINOR, quebra de compatibilidade → MAJOR (zera os campos à direita).
- A versão fica em `tvnviewer/tvnviewer.rc` (UTF-16 LE): `FILEVERSION`/`PRODUCTVERSION` numéricos `MAJOR,MINOR,FIX,0` e as strings `FileVersion`/`ProductVersion` com a versão completa (ex.: `0.1.0a`). O texto "Based on TightVNC Viewer 2.8.90" continua indicando a base.
- Registre a versão na mensagem de commit, no `MODIFICATIONS.md` e no `LEIAME.txt` (português) e no `README.txt` (inglês) do shipping, que acompanham os executáveis e o `LICENSE.txt`.
- Shipping na área de trabalho do dono: `Desktop\FarSightVNC-v<versão>\` + `.zip` (substitui a pasta/zip anterior), com apenas `fsvncviewer.exe`, `fsvncupdater.exe`, `LEIAME.txt`, `LICENSE.txt` e `README.txt` na raiz (layout plano; só 64 bits, sem versão 32 bits).
- Versão atual: `v1.0.0a`.

## Commits (obrigatório)

- **Commits atômicos**: cada commit contém uma única mudança lógica e coesa (uma correção, uma funcionalidade, um refactor, uma atualização de docs ou um bump de versão), que compila e passa nos testes sozinha. Não misture mudanças não relacionadas no mesmo commit; divida o trabalho em vários commits quando necessário.
- Mensagens no padrão Conventional Commits (`tipo(escopo): resumo`), com corpo explicando o quê e o porquê quando não for óbvio.
- Mudanças do dono que estejam no working tree vão em commits próprios, separados do trabalho dos agentes.
