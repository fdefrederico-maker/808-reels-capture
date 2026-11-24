# C00KUP · 808College

Plugin para o **Master do FL Studio (macOS)** que grava a sessão e entrega
automaticamente um **vídeo vertical 1080×1920 pronto para Reels/TikTok**:
câmara no topo, janela do FL em baixo, som do master, efeitos retro opcionais.

| Peça | Descrição |
|---|---|
| Plugin **C00KUP** (VST3 + AU, JUCE 8) | Loudness LUFS, medidores L/R, controlos de vídeo, pré-visualização 9:16 ao vivo, Gravar / Parar / Abrir pasta / Limpar pasta. Grava o master em WAV 48 kHz. |
| **C00KUP Engine** (Swift, sem ícone no Dock) | Aberto pelo plugin. Capta o FL (ScreenCaptureKit) e a câmara, troca o fundo, aplica efeitos e exporta o MP4 sincronizado. |

## Compilação

Cada `push` corre o workflow [`.github/workflows/build.yml`](.github/workflows/build.yml)
num Mac do GitHub: compila (Universal), assina ad-hoc, valida o AU, corre o autoteste do
motor e publica `C00KUP-vX.Y.Z.zip` na página **Releases**.

## Instalar

Ver [`packaging/LEIA-ME.txt`](packaging/LEIA-ME.txt) (também vem dentro do zip).

## Estrutura

```
design-c00kup/              Pacote de design (SPEC.md = fonte de verdade do visual)
plugin/                     Plugin JUCE (C++); plugin/Assets = fontes OFL e SVG embutidos
engine/                     Motor em Swift (SwiftPM) + ícone (AppIcon.iconset)
packaging/                  Instalador e LEIA-ME incluídos no zip
docs/DECISOES.md            Tudo o que foi feito e porquê
```
