# 808 Reels Capture

Ferramenta pessoal para gravar sessões do FL Studio (macOS, Apple Silicon) e
transformá-las em vídeos verticais 9:16 para Reels.

## Peças

| Peça | Estado | Descrição |
|---|---|---|
| Plugin JUCE (VST3 + AU) | Feito | Vai no Master do FL. Medidores de pico/RMS e loudness (LUFS). |
| Gravação WAV no plugin | Feito | Gravar / Parar; WAV 48 kHz / 24 bits com marca de tempo do relógio do sistema. |
| Motor Swift (sem ícone no Dock) | Feito (ecrã) | Capta a janela do FL, pré-visualização 9:16 ao vivo no plugin, exporta MP4 1080x1920 30 fps H.264 + AAC. |
| Câmara + fundo | Feito | Câmara numa faixa no topo (600 px), FL em baixo; troca da sala atrás da pessoa (desfocada, cor ou imagem) com Vision. |
| Título | Próxima fase | Texto sobre o vídeo. |

## Como funciona a sincronização

O plugin regista o `mach_absolute_time` do primeiro bloco de áudio gravado; o motor
regista o mesmo relógio no início do vídeo e escreve o frame *n* no instante
`início + n/30`. Na exportação, o WAV é deslocado pela diferença entre os dois.
O autoteste do motor (`ReelsEngine --selftest`, corre no GitHub Actions) confirma
que um flash na imagem e um clique no áudio ficam alinhados no MP4.

## Como é compilado

Não é preciso Mac de desenvolvimento nem Xcode. Cada vez que há um `push` para o
GitHub, o workflow [`.github/workflows/build.yml`](.github/workflows/build.yml)
corre num Mac da GitHub, compila o plugin e o motor (Universal: Apple Silicon + Intel),
assina-os com assinatura ad-hoc, corre os testes e publica um `.zip` na página **Releases**.

## Instalar

Veja [`packaging/LEIA-ME.txt`](packaging/LEIA-ME.txt) (também vem dentro do zip).

## Estrutura

```
CMakeLists.txt              Projeto principal (descarrega o JUCE 8 automaticamente)
plugin/CMakeLists.txt       Definição do plugin (nome, códigos, formatos)
plugin/Source/              Código C++ do plugin
engine/                     Motor em Swift (SwiftPM; a .app é montada no workflow)
packaging/                  Script de instalação e instruções incluídos no zip
.github/workflows/          Compilação automática no GitHub Actions
```
