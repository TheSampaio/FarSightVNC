# FarSightVNC — Relatório parcial da Fase 5 (Estabilidade e segurança)

- **Data:** 2026-11-02
- **Executado por:** Claude como gestor, com subagentes Claude (Sonnet)
- **Branch:** `feature/fase-5-estabilidade-seguranca`, worktree `D:\DevelopmentLibrary\Apps\FarsightVNC-fase5`, a partir de `develop` `a3640bc`
- **Status:** **parcial, encerrada antes do fim a pedido do dono.** A fase não está concluída.

## 1. Situação por item do plano

| Item | Situação |
|---|---|
| Documentar túnel SSH/VPN | Commit `902058a`, `docs/seguranca/tunel-ssh-vpn.md` |
| Avaliar VeNCrypt | Commit `902058a`, `docs/seguranca/avaliacao-vencrypt.md` |
| Atualizar zlib | **Incompleto e sem commit** (seção 2) |
| Fuzzing dos decoders | **Não iniciado** |
| Teste de sessão longa | **Não iniciado** |

## 2. zlib: estado deixado

- 26 arquivos alterados e **não commitados** em `zlib/`; `ZLIB_VERSION` já é 1.3.2.
- O subagente parou por limite de uso (HTTP 429), enquanto reverificava a assinatura.
- Downloads em `D:\DevelopmentLibrary\Apps\FarsightVNC-testenv\downloads\`: `zlib-1.2.13`, `zlib-1.3.2`, `zlib-1.3.2-gh`.
- **Não confirmado pelo gestor:** verificação de assinatura e hash, resultado do build e preservação de edições locais do TightVNC em relação ao zlib 1.2.13 original.
- `MODIFICATIONS.md` ainda não tem a linha do zlib.
- Não faça commit dessas alterações sem refazer essas verificações.

## 3. Resultados da avaliação

- **VeNCrypt:** recurso opcional, desligado por padrão, em fase posterior. Sugestão: mbedTLS sob GPL-2.0-or-later, começando por X509None/X509Vnc/X509Plain com trust-on-first-use. Estimativa do subagente: 3 a 5 semanas, ou 2 a 4 só com X509. A decisão é do dono.
- **Risco no servidor, informado pelo subagente:** a porta HTTP 5800 escutaria em 0.0.0.0 mesmo com `LoopbackOnly`, então `AcceptHttpConnections` deve ser desligado à parte. O gestor não conferiu esse trecho no código.

## 4. O que não foi verificado

- O subagente de documentação ficou sem acesso à web: detalhes de RFC, rfbproto, licenças e SChannel vieram de memória, e as URLs listadas não foram consultadas. Confira antes de decidir.
- Túnel SSH e regras de firewall não foram executados.
- A forma exata da opção `-port`, o significado de um único `:` e a troca dos gates de I/O durante a negociação são inferências.
- Nenhum build ou teste foi executado por esta sessão de gestão; não há evidência de build do zlib 1.3.2.

## 5. Próximos passos

1. Refazer as verificações do zlib, compilar as 4 configurações e o `tightvnc2017.sln` em Release|x64, atualizar `MODIFICATIONS.md` e commitar.
2. Fuzzing com servidor RFB falso (Tight, ZRLE, Hextile, RRE e mensagens malformadas). Não use `tools/test-server.ps1` nem `HKCU\Software\TightVNC\Server`, compartilhados com outras sessões; use porta própria.
3. Teste de sessão longa (memória e handles) reutilizando o servidor falso.
4. Integrar `develop`, que já avançou com a Fase 1 (`8307444`). O dono pediu que o Codex assumisse o restante, mas esse handoff **não foi feito**.
