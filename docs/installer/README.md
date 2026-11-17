# Instalador do viewer

O instalador é opcional; o zip portátil e o manifesto assinado do atualizador
continuam com os mesmos cinco arquivos na raiz. O Setup é um asset separado,
`FarSightVNC-v<versão>-Setup-x64.exe`, e nunca entra no zip ou no manifesto.

## Gerar

Requer Windows e [Inno Setup 6.3 ou posterior](https://jrsoftware.org/isdl.php).
Compile primeiro os dois executáveis e monte o shipping plano com somente
`fsvncviewer.exe`, `fsvncupdater.exe`, `LEIAME.txt`, `LICENSE.txt` e `README.txt`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\installer\Build-Installer.ps1 `
  -ShippingDirectory .\build\shipping\FarSightVNC-v1.3.0a
```

O script extrai a versão do viewer, exige a mesma versão no updater, verifica
os cabeçalhos PE x64 e rejeita arquivos extras, diretórios, links e arquivos
vazios. Ele procura `ISCC.exe` no PATH e nas pastas padrão; use `-CompilerPath`
para outro local. A saída padrão fica em `build\installer`. `-ValidateOnly`
valida o payload sem precisar do compilador e sem gerar o Setup. Os documentos
devem ser revisados para a mesma versão antes de montar o shipping.

## Comportamento

- Instala apenas o viewer e o updater para o usuário atual, em
  `%LOCALAPPDATA%\Programs\FarSightVNC`, sem solicitar elevação. Essa pasta
  permite que o atualizador existente troque os arquivos sem UAC.
- Adiciona um atalho no menu Iniciar, um atalho opcional na área de trabalho e
  uma entrada própria em Aplicativos instalados. O desinstalador gerado pelo
  Inno fica junto do programa; esses arquivos adicionais não entram no zip.
- Não instala servidor, serviço, driver, tarefa agendada ou regra de firewall.
  Não altera associações `.vnc` nem as configurações TightVNC/FarSightVNC.
  Desinstalar preserva configurações e arquivos do usuário.
- Usa um AppId estável, separado do TightVNC; uma nova execução do Setup
  reutiliza o diretório escolhido na instalação anterior do próprio FarSightVNC.
- Não fecha nem reinicia sessões automaticamente, inclusive em modo silencioso.
  Feche o viewer antes de instalar ou atualizar pelo Setup. Arquivos em uso
  podem impedir a instalação; não use instalação silenciosa durante sessões.
- A execução do programa ao concluir é opcional e vem desmarcada. O atualizador
  interno continua usando o zip e a assinatura ECDSA existentes.

O atualizador interno troca os cinco arquivos do aplicativo, mas não atualiza
os metadados do Inno no registro: a versão mostrada em Aplicativos instalados
pode continuar sendo a do último Setup executado. A versão mostrada pelo viewer
é a dos executáveis. Não há assinatura Authenticode configurada para o Setup;
a assinatura ECDSA do manifesto não assina o instalador.

## Estado

O Setup da v1.3.0a foi compilado e verificado aqui com o Inno Setup 6.7.3
(instalado por usuário): `FarSightVNC-v1.3.0a-Setup-x64.exe`, 2.995.849 bytes,
SHA-256 `C03C0633FD5425CA3A192384BC9D18515CCB12472E73EF5067A8E336CC9D8D86`,
FileVersion e ProductVersion `1.3.0a`, manifesto `asInvoker`.

Isso cobre apenas o passo 1 da validação abaixo. Instalar, reinstalar e
desinstalar **não** foram executados: faltam os passos 2 a 5, em VM ou conta
descartável. Um Setup compilado não é um Setup aceito.

## Validação antes de publicar

1. Compile o Setup com o shipping exato da versão e guarde o SHA-256 emitido.
2. Em uma VM ou conta descartável Windows x64, execute sem elevação. Confira
   diretório, atalhos, idiomas, versão e os cinco arquivos contra o shipping.
3. Conecte a um TightVNC existente e teste a atualização interna assinada.
4. Reinstale e atualize pelo Setup após fechar o viewer; confira que configurações
   e arquivos `.vnc` foram preservados.
5. Desinstale e confira a remoção dos arquivos/atalhos do instalador, preservação
   das configurações e funcionamento do TightVNC previamente instalado.

Não teste instalar/desinstalar na conta real do dono como parte do build.
Não publique o Setup antes dessa validação. O fluxo de release do zip/manifesto
continua em [RELEASING.md](../updater/RELEASING.md).

As decisões de instalação por usuário e de arquitetura seguem as diretivas
oficiais [PrivilegesRequired](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm)
e [Architecture identifiers](https://jrsoftware.org/ishelp/topic_archidentifiers.htm).
`CloseApplications=no` evita o fechamento automático descrito na documentação
de [CloseApplications](https://jrsoftware.org/ishelp/topic_setup_closeapplications.htm).
