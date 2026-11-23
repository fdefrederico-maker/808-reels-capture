# 808 Reels Capture

Ferramenta pessoal para gravar sessões do FL Studio (macOS, Apple Silicon) e
transformá-las em vídeos verticais 9:16 para Reels.

## Peças

| Peça | Estado | Descrição |
|---|---|---|
| Plugin JUCE (VST3 + AU) | **Fase 1** | Vai no Master do FL. Medidores de pico/RMS e loudness (LUFS). |
| Gravação WAV no plugin | Fase 2 | Botões Gravar / Parar / Exportar; WAV 48 kHz com marca de tempo. |
| Motor Swift (sem ícone no Dock) | Fase 3+ | Captura do ecrã do FL + câmara, pré-visualização 9:16, exportação MP4. |

## Como é compilado

Não é preciso Mac de desenvolvimento nem Xcode. Cada vez que há um `push` para o
GitHub, o workflow [`.github/workflows/build.yml`](.github/workflows/build.yml)
corre num Mac da GitHub, compila o plugin (Universal: Apple Silicon + Intel),
assina-o com assinatura ad-hoc e publica um `.zip` na página **Releases**.

## Instalar

Veja [`packaging/LEIA-ME.txt`](packaging/LEIA-ME.txt) (também vem dentro do zip).

## Estrutura

```
CMakeLists.txt              Projeto principal (descarrega o JUCE 8 automaticamente)
plugin/CMakeLists.txt       Definição do plugin (nome, códigos, formatos)
plugin/Source/              Código C++ do plugin
packaging/                  Script de instalação e instruções incluídos no zip
.github/workflows/          Compilação automática no GitHub Actions
```
