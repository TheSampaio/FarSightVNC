# FarSightVNC — Uso seguro: túnel SSH e VPN

- **Escopo:** guia de uso (nenhuma mudança de código). Comandos de firewall são apenas exemplos; não foram executados.
- **Legenda:** **[código]** = verificado no código-fonte deste repositório; **[spec]** = verificado na especificação RFB; **[inferido]** = não verificado em execução.

## 1. Por que um túnel é necessário

O protocolo RFB (RFC 6143) negocia um tipo de segurança (seção 7.1.2). O tipo "VNC Authentication" (seção 7.2.2) é um desafio-resposta: o servidor envia 16 bytes aleatórios, o cliente os cifra com DES usando a senha como chave e devolve o resultado.

| Protegido | Não protegido |
|---|---|
| A senha não trafega em claro (só o par desafio/resposta) | Todo o conteúdo depois da autenticação: imagem da tela, teclado, mouse, área de transferência e transferência de arquivos, em texto claro (RFC 6143, seção 7.3 e seguintes: nenhuma cifragem é definida) |
| | Integridade: um atacante no caminho pode alterar ou injetar mensagens, ou sequestrar a sessão após o login |
| | A senha em si fica exposta a ataque offline: desafio e resposta capturados permitem força bruta em DES com chave de no máximo 8 caracteres (RFC 6143, seção 7.2.2, cita a fraqueza) |

Conclusão: sem túnel ou VPN, qualquer pessoa na rede entre cliente e servidor vê e controla a sessão.

Fatos do código:
- **[código]** A senha é truncada a 8 caracteres antes de virar chave DES: `VNC_PASSWORD_SIZE = 8` (`server-config-lib/ServerConfig.h`) e `viewer-core/VncAuthentication.cpp` (`getSubstring(..., VNC_PASSWORD_SIZE - 1)` seguido de `memcpy` em buffer de 8 bytes). Caracteres além do 8º são ignorados; uma senha de 20 caracteres vale tanto quanto os 8 primeiros.
- **[código]** O viewer só tem os tratadores de autenticação None e VNC (mais Tight, que reutiliza o desafio VNC); não há tipo com TLS (`rfb/AuthDefs.h`). Ver a avaliação de VeNCrypt em `avaliacao-vencrypt.md`.

## 2. Opção A — Túnel SSH (recomendada para hosts pontuais)

### 2.1 Servidor (TightVNC Server) restrito ao loopback

Opções reais em `server-config-lib/Configurator.cpp` (chaves de registro/configuração):

| Chave | Efeito | Observação |
|---|---|---|
| `LoopbackOnly` (booleano) | **[código]** o servidor RFB faz bind em `localhost` em vez de `0.0.0.0` (`tvnserver-app/TvnServer.cpp`) e `rfb-sconn/RfbInitializer.cpp` rejeita conexões não loopback | Marcar "Only allow loopback connections" na GUI do servidor (aba de controle de acesso IP) |
| `AllowLoopback` (booleano) | **[código]** apenas permite conexões vindas de 127.0.0.1; **não** restringe as demais | Não substitui `LoopbackOnly` |
| `AcceptHttpConnections` (booleano) / `HttpPort` | **[código]** liga/desliga o servidor HTTP do applet Java (porta padrão 5800) | Desligar se não usado |
| `RfbPort` (padrão 5900) | **[código]** porta do servidor | |

**Atenção [código]:** em `TvnServer.cpp` o servidor HTTP é criado com bind em `0.0.0.0` mesmo com `LoopbackOnly` ativo (há um `FIXME` no código). Portanto `LoopbackOnly` **não** protege a porta 5800: desative `AcceptHttpConnections` explicitamente, ou bloqueie 5800 no firewall.

Cuidado: com `LoopbackOnly` o servidor só é alcançável pelo túnel que termina na própria máquina; teste o túnel antes de ativar em um servidor remoto sem outro acesso.

### 2.2 Cliente: OpenSSH do Windows

O Windows 10/11 inclui o cliente OpenSSH (`ssh.exe`; recurso opcional "OpenSSH Client").

```powershell
ssh -N -L 5901:127.0.0.1:5900 -o ServerAliveInterval=30 -o ServerAliveCountMax=3 -o ExitOnForwardFailure=yes usuario@servidor.exemplo
```

- `-L 5901:127.0.0.1:5900`: a porta local 5901 do cliente leva ao `127.0.0.1:5900` **visto pelo servidor SSH**. Por padrão o `-L` escuta só no loopback do cliente, que é o desejado; não use `-g` nem `0.0.0.0:` no bind.
- `-N`: não abre shell, só o encaminhamento.
- `ServerAliveInterval/CountMax`: derruba o túnel em ~90 s se o link cair (em vez de ficar travado); `ExitOnForwardFailure=yes` falha se a 5901 já estiver em uso.
- Use autenticação por chave (`ssh-keygen -t ed25519`, `-i`) e, no servidor SSH, `PasswordAuthentication no` quando possível.
- A primeira conexão pede confirmação da impressão digital do host; confira-a por outro canal (proteção contra MITM do próprio SSH).

### 2.3 Conectar o viewer

Sintaxe do endereço: **[código]** `tvnviewer/ViewerCmdLine.cpp` (`parseHostArg`) monta `host::porta` para porta TCP **absoluta** (`"%s::%s"`), logo:

```
127.0.0.1::5901
```

Equivalente em linha de comando: `tvnviewer.exe 127.0.0.1::5901` ou `-host=127.0.0.1 -port=5901` (**[código]** as opções `host`/`port` existem em `ViewerCmdLine`; a forma exata do `-port` depende do parser, **[inferido]** conferir com `-help`). Com um único `:` (ex.: `127.0.0.1:1`) a convenção TightVNC/RealVNC é número de display (5900 + N) **[inferido]**; use `::` para evitar ambiguidade. Porta padrão sem sufixo: 5900 (`LoginDialog::DEFAULT_PORT`).

## 3. Opção B — VPN

Qualquer VPN que dê ao servidor um endereço privado e criptografe o tráfego serve: WireGuard, malha tipo Tailscale, VPN corporativa (IPsec/OpenVPN). O VNC passa a ser tráfego interno à VPN; a cifragem é a da VPN, não do VNC.

Regras:
1. Não exponha 5900 (nem 5800) na interface pública. Libere só para a interface/sub-rede da VPN.
2. Prefira VPN com autenticação por chave/identidade (não só senha compartilhada) e controle de quais pares alcançam o servidor (ACL).

Exemplo de firewall do Windows (somente documentação; requer PowerShell elevado; ajuste o `RemoteAddress` à sub-rede da sua VPN, aqui `10.8.0.0/24`):

```powershell
# Permite o VNC apenas a partir da sub-rede da VPN
New-NetFirewallRule -DisplayName "VNC somente VPN" -Direction Inbound -Protocol TCP `
  -LocalPort 5900 -RemoteAddress 10.8.0.0/24 -Action Allow -Profile Any

# Bloqueia o applet HTTP (5800) para todos, caso o servidor HTTP esteja ativo
New-NetFirewallRule -DisplayName "VNC HTTP bloqueado" -Direction Inbound -Protocol TCP `
  -LocalPort 5800 -Action Block -Profile Any
```

Observações: o instalador do TightVNC pode ter criado regras próprias que permitem 5900 de qualquer origem; revise com `Get-NetFirewallRule -DisplayName *VNC*` e remova ou restrinja as amplas **[inferido]** (não verificado nesta máquina). Regras de bloqueio têm precedência sobre as de permissão no Windows Defender Firewall.

## 4. Lista de verificação

- [ ] Servidor: `LoopbackOnly` ativo (túnel SSH) **ou** 5900 restrita à VPN por firewall.
- [ ] `AcceptHttpConnections` desligado; 5800 bloqueada.
- [ ] Nenhum redirecionamento de porta no roteador para 5900/5800.
- [ ] Túnel com chave SSH, `ServerAliveInterval`, bind local apenas em loopback.
- [ ] Senha VNC forte nos **8 primeiros caracteres**, única (não reutilizada de contas, SSH ou Windows).
- [ ] Senha somente-leitura distinta da principal, se usar o modo de visualização.
- [ ] Verificar que o viewer conecta em `127.0.0.1::5901` e que `Test-NetConnection servidor -Port 5900` **falha** de fora da VPN/túnel.

## 5. Erros comuns

1. **Expor 5900 diretamente na Internet.** Mecanismos de varredura indexam VNC aberto; sem túnel, o tráfego é legível e a senha é atacável offline.
2. **Achar que senha longa protege.** Só 8 caracteres contam [código]; entropia máxima limitada, e DES é fraco.
3. **Reusar a senha** do VNC em outros serviços: o par desafio/resposta capturado permite força bruta offline.
4. **Confiar que `LoopbackOnly` cobre tudo.** Não cobre a porta HTTP 5800 [código].
5. **Túnel sem keepalive:** a sessão fica "congelada" quando o link cai.
6. **Bind do túnel em `0.0.0.0`** (ex.: `-L 0.0.0.0:5901:...`): reexpõe o VNC sem cifragem na rede local do cliente.
7. **Ignorar o aviso de chave de host do SSH** na primeira conexão ou quando ela muda.

## Fontes

- RFC 6143, "The Remote Framebuffer Protocol", seções 7.1.2, 7.2.1, 7.2.2: https://www.rfc-editor.org/rfc/rfc6143
- Microsoft Learn, "Get started with OpenSSH for Windows" e `New-NetFirewallRule`: https://learn.microsoft.com/windows-server/administration/openssh/openssh-overview ; https://learn.microsoft.com/powershell/module/netsecurity/new-netfirewallrule
- OpenSSH `ssh(1)` / `ssh_config(5)` (opções `-L`, `-N`, `ServerAliveInterval`, `ExitOnForwardFailure`): https://man.openbsd.org/ssh

Não verificado: execução real do túnel e do viewer, sintaxe exata de `-port`, estado das regras de firewall criadas pelo instalador. As URLs acima foram citadas de memória, sem consulta online nesta sessão.
