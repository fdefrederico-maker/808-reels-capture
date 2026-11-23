#!/bin/bash
# Instala o plugin "808 Reels Capture" (VST3 + AU) na pasta de plug-ins do utilizador.
# Uso (no Terminal):  bash ~/Downloads/808-Reels-Capture/instalar.sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"

echo "A remover a marca de 'descarregado da Internet'..."
xattr -dr com.apple.quarantine "$DIR" 2>/dev/null || true

mkdir -p "$VST3_DEST" "$AU_DEST"

echo "A copiar o VST3 para $VST3_DEST"
rm -rf "$VST3_DEST/808 Reels Capture.vst3"
cp -R "$DIR/808 Reels Capture.vst3" "$VST3_DEST/"

echo "A copiar o AU para $AU_DEST"
rm -rf "$AU_DEST/808 Reels Capture.component"
cp -R "$DIR/808 Reels Capture.component" "$AU_DEST/"

# Obriga o macOS a voltar a ler a lista de Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "Instalado com sucesso."
echo "Agora abra o FL Studio e faça: Options > Manage plugins > Find more plugins."
