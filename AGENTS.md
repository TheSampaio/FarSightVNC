# FarSightVNC — instruções para agentes

## Versionamento (obrigatório)

- Formato: `vMAJOR.MINOR.FIXs`, onde o sufixo `s` é `a` (alpha), `b` (beta) ou nenhum (estável). Ex.: `v1.2.5a`.
- **Toda mudança entregue (build enviado ao dono / shipping) incrementa a versão**: correção → FIX, funcionalidade → MINOR, quebra de compatibilidade → MAJOR (zera os campos à direita).
- A versão fica em `tvnviewer/tvnviewer.rc` (UTF-16 LE): `FILEVERSION`/`PRODUCTVERSION` numéricos `MAJOR,MINOR,FIX,0` e as strings `FileVersion`/`ProductVersion` com a versão completa (ex.: `0.1.0a`). O texto "Based on TightVNC Viewer 2.8.90" continua indicando a base.
- Registre a versão na mensagem de commit, no `MODIFICATIONS.md` e no `LEIA-ME.txt` do shipping.
- Shipping na área de trabalho do dono: `Desktop\FarSightVNC-v<versão>\` + `.zip` (substitui a pasta/zip anterior).
- Versão atual: `v0.2.1a`.
