# Fase 1 — teclado

Data: 2026-11-01. Integração/revisão: Codex, a partir do trabalho incompleto
em `FarSightVNC-wt/fase-1`. Implementação na branch `develop`; os worktrees
dos Claudes foram preservados.

- Press/release usa scancode + extended e conserva o keysym original.
- Shift direito usa scancode 0x36. Autorepeat repete o keysym armazenado;
  modificadores repetidos não geram transições extras.
- ToUnicodeEx consulta sem alterar o estado (bit 2), antes de TranslateMessage.
  O loop entrega imediatamente os caracteres da composição antes da próxima tecla.
- Hook respeita foco/foreground, preserva scancode inteiro e ignora injetados.
  Modificadores físicos compartilham a fila; locks permanecem a cargo do sistema.
- Perda de foco libera também teclas comuns e modificadores da toolbar.
- Alt/Meta conserva o símbolo de release mesmo se Scroll Lock mudar.
- Log de diagnóstico de VK/dados usa o nível debug existente.

Validação: 188 testes por configuração, zero falhas em Release/Debug x64/Win32.
O harness não injeta entrada no desktop e não conecta ao serviço VNC instalado.
Resultados: `build-logs/keyboard-*.txt`. Build do Viewer nas quatro configurações:
`build-logs/phase1-viewer-build.txt`.

Limitações: o harness simula a parte relevante do loop de mensagens; não comprova
interoperabilidade com todos os servidores nem hook real sob carga. IME japonês,
Windows anterior ao suporte ao bit 2 de ToUnicodeEx (Windows 10 1607), e uso
prolongado ainda não validados. Não há opção de voltar ao tradutor antigo.
O teste de teclado não é um teste de toda a aplicação.

Próxima fase: 2 — DPI, janela e monitores. O histórico de commits e este relatório
são o ponto de continuidade; não sobrescrever develop com os snapshots antigos.