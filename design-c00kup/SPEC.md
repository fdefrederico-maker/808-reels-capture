# C00KUP · especificação da interface (direção D)

Referência visual: `design/C00KUP-interface@2x.png` (parado) e `design/C00KUP-interface-a-gravar@2x.png` (a gravar).
Os ficheiros `.html` ao lado são a mesma interface em HTML, com todas as medidas exatas: em caso de dúvida, valem eles.

Janela fixa **760 × 600 px**. Todas as medidas abaixo estão em px a 1x.

## Cores

| Token | Hex | Uso |
|---|---|---|
| fundo | `#0A0A0A` | fundo da janela |
| vermelho | `#C61621` | lombada, disco do Gravar, LED, arcos dos knobs, marca 0 dB, timecode a gravar |
| marfim | `#F1ECE4` | texto principal, barras dos medidores, ponteiros, "00" do wordmark |
| cinza texto | `#8C877F` | rótulos, unidades, legendas |
| cinza botão | `#C9C3B6` | texto de botões secundários |
| superfície | `#161616` | botões |
| borda botão | `#2C2C2C` | bordas dos botões |
| campo | `#050505` | campos rebaixados e interruptor |
| linha | `#1F1F1F` | divisórias e realce inferior dos campos |
| trilho | `#1D1D1D` / `#262626` | trilho dos medidores / trilho dos knobs |
| knob | `#1A1A1A` (borda `#2F2F2F`) | corpo dos knobs |
| sombra disco | `#2A1214` | anel de 3 px do disco vermelho |

## Fontes (pasta `fonts/`, licença OFL incluída)

- **Schibsted Grotesk**: Medium 500 (valores dos campos), Bold 700 (botões, short-term), Black 900 (número de loudness).
- **Martian Mono**: Regular 400 (rótulos, valores, timecode).
- Embutir com BinaryData do JUCE e `Typeface::createSystemTypefaceFor`. Nunca depender das fontes do sistema.

## Estrutura

1. **Lombada** (x 0–76, altura toda): fundo vermelho.
   - Símbolo `assets/simbolo-808-preto.svg` com 40 px de altura, centrado, a 22 px do topo.
   - Wordmark `assets/c00kup-wordmark.svg` rodado −90°, centrado em x 38 e y 336, com cerca de 300 px de comprimento. Letras pretas `#0A0A0A` e o **"00" em marfim** (é isto que faz ler C00KUP e não COOKUP; está desenhado no SVG).
2. **Área principal** (x 76–760): margens de 26 em cima, 32 dos lados e 22 em baixo, 18 entre blocos.
3. **Topo (loudness)**:
   - Rótulo "LOUDNESS · MOMENTARY" em Martian 9 px, maiúsculas, espaçamento 0,08 em, cinza.
   - Número momentary em Schibsted Black 88 px, espaçamento −0,05 em, marfim, com "LUFS" ao lado em Martian 11 px cinza.
   - À direita, de cima para baixo: botão **LIMPAR PASTA**, short-term em Schibsted Bold 24 px seguido de "SHORT" em Martian 9 px, e "alvo Reels −14 · 48 kHz" em Martian 9 px.
4. **Medidores L/R**: grelha de 12 | resto | 72 px, com 12 de espaço.
   - Barra com 4 px de altura: trilho `#1D1D1D`, enchimento marfim.
   - Marca de 0 dB: linha vermelha de 1 px aos 90% da largura, que sai 5 px para cima e para baixo.
   - Valor à direita: "−10.5 máx" em Martian 9 px. Clicar no valor limpa o máximo.
   - 10 px entre L e R.
5. **Meio**: coluna de controlos | pré-visualização de 160 px, com 32 de espaço entre elas.
   - Cada linha de controlo tem um rótulo de 70 px, 12 de espaço, altura mínima de 40 e uma linha de 1 px `#1F1F1F` por cima. Rótulos em Martian 9 px maiúsculas cinza.
   - **Câmara**: texto "No topo" em Schibsted Medium 13. Interruptor de 38×20 com raio 10, fundo `#050505`, borda `#000` e realce inferior `#1F1F1F`; bolinha marfim de 14 px. Ao lado, um LED vermelho de 7 px com brilho de 6 px quando ligado (apagado: `#2A1214`, sem brilho).
   - **Fundo**: campo rebaixado com 30 de altura, padding 10, fundo `#050505`, borda `#000`, sombra interior de 1 px `#000` em cima, realce de 1 px `#1F1F1F` em baixo, raio 3, texto Schibsted Medium 13 marfim e chevron de 10×6 com traço 1,5. Ao lado, o botão **IMAGEM**, do mesmo estilo do Limpar pasta mas com 30 de altura.
   - **Efeito**: o mesmo campo rebaixado, com a largura toda.
   - **Knobs** (Zoom FL, Altura, Sincronia), distribuídos pela largura, com 14 de espaço por cima e uma linha antes:
     - Knob de 48×48 com arco de raio 19 e traço 3 com pontas redondas.
     - O trilho `#262626` vai de 135° a 405° (270° de curso).
     - O arco de valor é vermelho. No **Zoom** começa no mínimo. **Altura e Sincronia são bipolares**: o arco parte do topo (zero) para cada lado.
     - Corpo: círculo de raio 13, `#1A1A1A`, borda `#2F2F2F`. Ponteiro de 2 px marfim, do centro até ao raio 10.
     - Por baixo: rótulo em Martian 9 px e valor em Martian 10 px marfim.
     - Arrastar na vertical muda o valor, duplo clique repõe o padrão e a roda do rato também funciona.
   - **Pré-visualização**: 160×284 (9:16), borda de 1 px `#262626`, fundo `#121316`, com a legenda "ARRASTE · RODA = ZOOM" por baixo. Mostra o vídeo real ao vivo. A posição esquerda/direita, que antes tinha barra, passa a fazer-se só a arrastar aqui.
6. **Rodapé**: linha de 1 px `#1F1F1F` em cima e 14 de padding.
   - **GRAVAR**: pílula com 52 de altura e raio 26, fundo `#161616`, borda `#2C2C2C`, padding 0 22 0 10. Disco vermelho de 32 px com anel de 3 px `#2A1214`. Texto Schibsted Bold 13 px, espaçamento 0,22 em, marfim.
   - **PARAR**: a mesma pílula. Parado: fundo `#111111`, borda `#222222`, texto `#6E6A62`, quadrado de 12 px `#4A4842`. A gravar: as cores do Gravar, com o quadrado marfim.
   - **ABRIR PASTA**: link em Schibsted Bold 11 px, espaçamento 0,2 em, `#C9C3B6`, sublinhado com 4 px de afastamento.
   - À direita: timecode em Martian 16 px (marfim; vermelho a gravar) e estado em Martian 8 px, espaçamento 0,1 em, cinza.

## Estados

- **A gravar**:
  - O disco do Gravar ganha um anel de 6 px a 25% de vermelho e um brilho de 18 px a 60%, que pulsa a 1 Hz.
  - O texto passa a "A GRAVAR" e o Parar fica ativo.
  - O timecode fica vermelho e o estado mostra "A GRAVAR · 1080×1920".
  - O Limpar pasta fica desativado.
- **Desativado**: opacidade de 40% e sem hover.
- **Hover**: a borda dos botões passa a `#3A3A3A` e os links a marfim.
- **Foco por teclado**: contorno de 2 px marfim, afastado 2 px.
- **Mensagens de estado** (motor não instalado, a exportar, exportado): no texto de estado por baixo do timecode, em maiúsculas. Erros em vermelho.

## Nome e identificadores

| Campo | Valor |
|---|---|
| Nome do produto | `C00KUP` (com zeros) |
| Fabricante / vendor | `808College` |
| Código do fabricante | `Cl8g` |
| Código do plugin | `C00k` |
| Bundle ID do plugin | `com.808college.c00kup` |
| Motor | `C00KUP Engine`, bundle `com.808college.c00kup.engine` |
| Pasta dos vídeos | `Filmes › C00KUP` (brutos em `C00KUP/Brutos`) |
| Ficheiros | `C00KUP 2026-11-24 22-10-05.mp4` |
