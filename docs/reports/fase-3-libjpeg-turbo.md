<!-- Modified for FarSightVNC on 2026-11-02: written-only JPEG dependency assessment. -->
# Fase 3: avaliação do libjpeg-turbo

Data: 2026-11-02. Decisão: não adicionar nem substituir dependências nesta fase.

O projeto inclui IJG libjpeg 9e (`libjpeg/jpeglib.h`: versão 90, major 9,
minor 5). `viewer-core/JpegDecompressor.cpp` usa a API libjpeg, buffers em
memória, validação das dimensões e saída RGB de três bytes por pixel;
`TightDecoder.cpp` usa esse caminho somente para retângulos JPEG do Tight.
O benchmark Raw desta fase não executa essa descompressão e, portanto, não
mede o benefício de trocar a biblioteca.

A documentação oficial do libjpeg-turbo descreve aceleração SIMD e as APIs
libjpeg e TurboJPEG. A compatibilidade ABI declarada cobre v6b e,
opcionalmente, v7/v8; a ABI v9 não é emulada. Assim, uma eventual adoção
precisa recompilar todos os consumidores com headers compatíveis, conferir
estruturas e tratamento de erros e validar o caminho JPEG do Tight. Não se
trata de trocar um binário mantendo os headers 9e existentes. Não há vantagem
intrínseca de desempenho entre as duas APIs para operações equivalentes.
[README oficial](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/README.md).

O estudo publicado de desempenho usa versões e cargas específicas e avisa
que seus dados são históricos. Seus fatores de aceleração não demonstram
melhora neste viewer nem explicam resultados de uma sessão Raw.
[Estudo oficial](https://libjpeg-turbo.org/About/Performance).

Para uma proposta futura: primeiro perfilar sessões Tight/JPEG representativas
e quantificar a parcela de CPU de `JpegDecompressor`; depois, com aprovação da
dependência, comparar os mesmos fluxos JPEG em x64/Win32, imagens válidas e
malformadas, limites de dimensões, cores e regressões de qualidade. Amdahl
limita o ganho total à fração efetivamente gasta no codec. O orçamento de
build/manutenção e a revisão dos avisos de licença do pacote escolhido também
fazem parte dessa decisão. Nenhum pacote foi baixado ou código JPEG alterado
para esta avaliação.