# Prompt para colar no Claude Code

Cola o texto abaixo numa sessão nova do Claude Code ligada ao repositório `808-reels-capture`, depois de teres enviado a pasta `design-c00kup` para o repositório (passos no LEIA-ME).

---

Vamos dar ao plugin o nome e o visual finais. A pasta `design-c00kup/` tem tudo:
- `SPEC.md`: cores, fontes, medidas, estados e identificadores. É a fonte de verdade.
- `design/C00KUP-interface@2x.png` e `design/C00KUP-interface-a-gravar@2x.png`: como tem de ficar, parado e a gravar.
- `design/*.html`: a mesma interface em HTML, com todas as medidas exatas.
- `fonts/`: Schibsted Grotesk e Martian Mono em TTF, com licença OFL.
- `assets/`: símbolo 808College e wordmark C00KUP em SVG.

Faz assim, por esta ordem:

1. **Nome e vendor.** Muda o produto para `C00KUP`, o fabricante para `808College`, os códigos para `Cl8g` e `C00k`, o bundle para `com.808college.c00kup`, o motor para `C00KUP Engine` (bundle `com.808college.c00kup.engine`), a pasta de vídeos para `Filmes › C00KUP` e o nome dos ficheiros, das Releases, do instalador e do LEIA-ME. Ainda não uso o plugin em projetos a sério, por isso podes mudar os códigos. Atualiza a porta IPC só se for preciso.
2. **Fontes e assets.** Embute as TTF e os SVG com BinaryData do JUCE. Usa `Typeface::createSystemTypefaceFor` para as fontes e `Drawable::createFromSVG` para o símbolo e o wordmark, que vai rodado −90° na lombada. Nada de fontes do sistema.
3. **Interface.** Refaz o `PluginEditor` igual à SPEC e às imagens, com um `LookAndFeel` próprio para os knobs, os interruptores, os campos rebaixados e as pílulas. As funcionalidades e os parâmetros ficam todos como estão. Muda só o aspeto, com duas exceções:
   - Zoom FL, Altura e Sincronia passam a ser knobs rotativos (Altura e Sincronia bipolares).
   - A barra de posição por baixo da pré-visualização sai. A posição faz-se a arrastar na própria pré-visualização, como já funciona.
4. **Estados.** Implementa o estado "a gravar" (disco a pulsar, timecode vermelho, Parar ativo, Limpar pasta desativado), o hover, o foco por teclado e as mensagens de estado como diz a SPEC.
5. **Ícone do motor.** Cria um ícone para o `C00KUP Engine` com o símbolo 808College preto sobre o vermelho `#C61621`.
6. Atualiza `docs/DECISOES.md` e gera uma Release nova.

No fim, diz-me como instalar a versão nova por cima da antiga, explicado passo a passo como se eu nunca o tivesse feito. Inclui o que devo apagar da versão "808 Reels Capture", porque o FL vai ver o C00KUP como um plugin novo.
