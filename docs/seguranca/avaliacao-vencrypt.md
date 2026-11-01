# FarSightVNC — Avaliação: VeNCrypt (tipo de segurança 19) como recurso opcional do cliente

- **Natureza:** avaliação, **sem implementação**. Nenhum código foi alterado.
- **Legenda:** **[código]** verificado neste repositório; **[spec]** conforme a especificação rfbproto (de memória, não reconsultada online nesta sessão); **[inferido]** raciocínio sem verificação.
- **Motivação:** o TightVNC Server não oferece VeNCrypt, mas servidores de terceiros oferecem (TigerVNC, QEMU, libvncserver/x11vnc). Um viewer com VeNCrypt cifraria a sessão sem túnel SSH nesses servidores.

## 1. Resumo do protocolo [spec]

Após o servidor anunciar o tipo de segurança 19 e o cliente escolhê-lo:

1. Troca de versão do VeNCrypt (servidor envia major.minor; versão 0.2 é a usada na prática; cliente responde com a que aceita; servidor responde 0 = OK).
2. Servidor lista subtipos (u8 contagem + u32 por subtipo); o cliente escolhe um e o ecoa (servidor confirma com u8 não-zero).
3. Para subtipos TLS/X509, o handshake TLS acontece **dentro da conexão TCP já aberta**, e o restante (autenticação do subtipo, ClientInit, ServerInit e mensagens RFB) passa a trafegar cifrado.

| Subtipo | Código | TLS | Autenticação interna |
|---|---|---|---|
| Plain | 256 | nenhum | usuário/senha em claro (sem cifra) |
| TLSNone | 257 | TLS anônimo | nenhuma |
| TLSVnc | 258 | TLS anônimo | VNC (DES) |
| TLSPlain | 259 | TLS anônimo | usuário/senha |
| X509None | 260 | TLS com certificado X.509 | nenhuma |
| X509Vnc | 261 | X.509 | VNC (DES) |
| X509Plain | 262 | X.509 | usuário/senha |

Pontos críticos:
- **TLS anônimo (TLS*) usa suítes ADH/anon-ECDH** [spec], sem certificado. **O SChannel do Windows não suporta Diffie-Hellman anônimo** [inferido a partir de documentação Microsoft: suítes anônimas não estão entre as suítes SChannel suportadas]; logo SChannel só serviria aos subtipos X509*. OpenSSL moderno exige `@SECLEVEL=0` para ADH; mbedTLS e GnuTLS (`NORMAL:+ANON-ECDH:+ANON-DH`) suportam, mbedTLS dependendo de configuração.
- **TLS anônimo não protege contra MITM**: cifra contra espião passivo, mas qualquer intermediário ativo faz TLS anônimo com os dois lados. Deve ser apresentado ao usuário como "cifrado, não autenticado".
- Subtipos X509 protegem contra MITM apenas se o certificado for **validado** (cadeia, nome, expiração ou fixação/TOFU). Muitos servidores usam certificado autoassinado.
- Subtipos `*Vnc` repetem a limitação das senhas de 8 caracteres/DES, porém dentro de canal TLS.
- Plain (256) não cifra; o cliente deveria **recusá-lo** por padrão.

Referência: especificação comunitária rfbproto (https://github.com/rfbproto/rfbproto, `rfbproto.rst`, seção "VeNCrypt").

## 2. Onde se encaixaria neste código

### Negociação de tipo de segurança [código]
- `viewer-core/RemoteViewerCore.cpp`: `negotiateSecurityType()` lê a lista (`readSecurityTypeList`), escolhe com `selectSecurityType()` e escreve o escolhido. `selectSecurityType` prefere Tight (16) se habilitado, senão o primeiro tipo em que exista tratador em `m_authHandlers` ou seja `NONE`. Tipo 19 hoje cairia em "No security types supported".
- `rfb/AuthDefs.h`: `SecurityDefs` define `INVALID/NONE/VNC/TIGHT`; é onde entraria `VENCRYPT = 19` (e os subtipos 256–262).
- `viewer-core/AuthHandler.h`, `VncAuthenticationHandler.*`, `VncAuthentication.*`: interface `AuthHandler::authenticate(DataInputStream*, DataOutputStream*)` e a rotina DES (`vncAuthenticate`) que os subtipos `*Vnc` reutilizariam.
- Após a autenticação, `RemoteViewerCore` lê `SecurityResult` (u32) e trata falha como `AuthException`.

### Abstração de transporte [código]
- `viewer-core/TcpConnection.cpp`: cria `SocketIPv4` → `SocketStream` → `BufferedInputStream` → `RfbInputGate`/`RfbOutputGate`. `RemoteViewerCore` usa `m_input`/`m_output` (gates) para todo o protocolo, e `TcpConnection` aceita gates já prontos (ramo `m_input != 0 && m_output != 0`).
- Consequência: o ponto de integração limpo é um **par de streams TLS** (`TlsInputStream`/`TlsOutputStream`, ou um `TlsStream` implementando as interfaces de `io-lib`: `InputStream`/`OutputStream`) que envolva `SocketStream`, substituindo o `BufferedInputStream`/`RfbOutputGate` assentados sobre o socket depois do handshake. Hoje `m_input`/`m_output` são membros de `RemoteViewerCore` ligados no `start`; seria preciso permitir **reatribuí-los no meio da negociação** (o `m_updateRequestSender.setOutput(m_output)` na linha ~563 guarda o ponteiro e precisaria ser religado) **[inferido]**.
- Observação importante: `BufferedInputStream` pode já ter lido bytes do socket antes do handshake TLS. O TLS deve consumir esse buffer residual primeiro, ou o handshake deve rodar sobre o socket cru antes de criar o buffer **[inferido]**.
- Interface de usuário: pedir usuário/senha (TLSPlain/X509Plain) exige um diálogo novo em `tvnviewer/` (hoje só senha, pela `LoginDialog`) e confirmação de certificado (TOFU).

## 3. Opções de biblioteca TLS (GPLv2-ou-posterior)

O FarSightVNC é distribuído sob GPLv2+. Licenças de bibliotecas (conforme conhecimento geral; confirmar nos textos oficiais antes de decidir):

| Biblioteca | Licença | Compatível com GPLv2+? | ADH/anon | Notas |
|---|---|---|---|---|
| **SChannel** | componente do sistema (exceção "System Library" da GPL) | Sim | **Não** | Zero dependências e atualizações via Windows; API pesada e verbosa; só X509 |
| **OpenSSL 3.x** | Apache-2.0 | **Só com GPLv3**; Apache-2.0 é incompatível com GPLv2-only. Como o projeto é "v2 ou posterior", o binário combinado poderia ser distribuído sob GPLv3, **mas isso muda os termos efetivos e deve ser decisão explícita do dono** | Sim (`@SECLEVEL=0`) | Mais difundida, binário grande |
| **mbedTLS** | Apache-2.0 **ou** GPL-2.0-or-later (dual) | **Sim**, escolhendo GPL-2.0+ | Sim (configurável) | Pequena, fácil de embutir e compilar com MSVC; boa para este tamanho de projeto |
| **GnuTLS** | LGPL-2.1+ | Sim | Sim | Build no Windows/MSVC é trabalhoso (dependências nettle/gmp) |
| **BearSSL** | MIT | Sim | **Não** (sem suítes anônimas) [inferido] | Muito pequena e segura por projeto, mas sem ADH e com validação X.509 mínima |

## 4. UX de validação de certificado

- Subtipos X509: validar a cadeia contra o repositório do Windows quando houver CA pública; para autoassinado, mostrar impressão digital SHA-256 e oferecer **TOFU** (fixar na primeira vez, alertar se mudar), armazenando em `%APPDATA%` (nunca no histórico de conexões existente).
- Nunca aceitar silenciosamente certificado inválido; o padrão deve ser recusar.
- Subtipos TLS anônimos: mostrar indicador "cifrado, servidor não autenticado" e, por padrão, não oferecê-los (ou pedir confirmação por conexão).
- Recusar Plain (256).

## 5. Estimativa de esforço (aproximada, 1 desenvolvedor)

| Atividade | Estimativa |
|---|---|
| Subtipos X509None/X509Vnc/X509Plain com SChannel ou mbedTLS | 8–15 dias |
| Adicionar TLS anônimo (exige mbedTLS/GnuTLS; SChannel não serve) | +2–4 dias |
| Camada `TlsStream` e religação dos gates (§2) | incluída acima; 3–5 dias da parte total |
| UI (usuário/senha, confirmação de certificado, TOFU, armazenamento) | 4–8 dias |
| Testes, fuzz, ambiente de servidores | 5–8 dias |
| **Total** | **~3–5 semanas** (X509 apenas ~2–4 semanas) |

Estimativas são palpites, sem protótipo.

## 6. Riscos

- Licenciamento: escolha errada (OpenSSL) força GPLv3 ou viola a GPLv2.
- Superfície de ataque nova: parser do handshake e biblioteca TLS recebem bytes de servidor não confiável; CVEs da biblioteca passam a ser responsabilidade do projeto (atualização).
- Falsa sensação de segurança com TLS anônimo ou aceitação cega de certificados.
- Regressão no caminho existente: a troca de gates no meio da negociação toca `RemoteViewerCore` (núcleo crítico).
- Tamanho do binário e build reproduzível (`tools/build.ps1`) com dependência nova.
- Manutenção: o VeNCrypt é pouco documentado; diferenças entre servidores (TigerVNC vs. QEMU vs. libvncserver) causam casos de borda.

## 7. Plano de testes

1. Servidor TigerVNC (`Xvnc`/`vncserver` com `-SecurityTypes X509Vnc,TLSVnc`) em VM Linux ou WSL, acessado por loopback/rede host-only.
2. QEMU (`-vnc ...,tls-creds=...`) e x11vnc (`-ssl`/`-sslonly`) para diversidade de implementações.
3. Casos negativos: certificado expirado, nome incorreto, cadeia desconhecida, mudança de certificado (TOFU), servidor que só oferece Plain, versão VeNCrypt desconhecida, desconexão no meio do handshake.
4. Fuzz do parser da negociação VeNCrypt (lista de subtipos, tamanhos, versão) com um servidor falso (padrão de `tools/test-server.ps1`, somente loopback) e fuzzers de bytes aleatórios/truncados; sanitizers/Application Verifier.
5. Regressão: `tools/smoke-test.ps1` e a medição de recursos contra o TightVNC Server (sem TLS) devem permanecer inalterados.
6. Verificar que o histórico do viewer e a configuração do usuário não são alterados pelos testes (convenção já usada nos scripts).

## 8. Recomendação

1. **Fazer, como recurso opcional e desligado por padrão**, em fase posterior à estabilização (não bloqueia a Fase 5).
2. **Biblioteca: mbedTLS**, usada sob a opção GPL-2.0-or-later da dual license: compatível com GPLv2+, pequena, suporta TLS anônimo e X.509. SChannel é a alternativa se o dono aceitar escopo apenas X509 e priorizar zero dependências. Evitar OpenSSL salvo decisão de migrar o projeto para GPLv3.
3. **Subtipos: X509Vnc, X509Plain e X509None primeiro** (autenticados por certificado + TOFU). TLSVnc/TLSNone/TLSPlain somente se houver demanda, com aviso "sem proteção contra MITM". Recusar Plain.
4. Até lá, a recomendação operacional continua sendo o túnel SSH/VPN (`tunel-ssh-vpn.md`).

### Decisões necessárias do dono
- Aceitar ou não migrar a distribuição para GPLv3 (liberaria OpenSSL).
- mbedTLS (anon + X509) ou SChannel (só X509, sem dependência)?
- Incluir TLS anônimo, apesar de não proteger contra MITM?
- Política de certificados autoassinados (TOFU) e onde guardar as impressões digitais.
- Prioridade e fase para a implementação.

## O que está verificado e o que não está

- **Verificado no código:** fluxo de negociação (`RemoteViewerCore::negotiateSecurityType/selectSecurityType`), tipos definidos em `rfb/AuthDefs.h`, cadeia de streams em `TcpConnection`, interface `AuthHandler`.
- **Inferido/não verificado:** detalhes do protocolo VeNCrypt (códigos e sequência, de memória da especificação rfbproto), ausência de ADH no SChannel, licenças (conhecimento geral), esforço, viabilidade de religar gates sem efeitos colaterais, comportamento do `BufferedInputStream` residual. Nenhuma fonte online foi consultada nesta sessão; confirmar antes de decidir.

## Fontes

- rfbproto (especificação comunitária do RFB, inclui VeNCrypt): https://github.com/rfbproto/rfbproto
- RFC 6143: https://www.rfc-editor.org/rfc/rfc6143
- mbedTLS, licença dual Apache-2.0/GPL-2.0-or-later: https://github.com/Mbed-TLS/mbedtls (arquivo LICENSE / README)
- Compatibilidade Apache-2.0 e GPL: https://www.apache.org/licenses/GPL-compatibility.html
- Microsoft, suítes de cifra do SChannel: https://learn.microsoft.com/windows/win32/secauthn/cipher-suites-in-schannel
- GnuTLS (LGPL-2.1+): https://www.gnutls.org/ ; BearSSL (MIT): https://bearssl.org/
