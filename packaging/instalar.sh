#!/bin/bash
# Instala o 808 Reels Capture: plugin (VST3 + AU) e motor de vídeo.
# Uso (no Terminal):  bash ~/Downloads/808-Reels-Capture/instalar.sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"
APP_DEST="$HOME/Applications"

echo "A remover a marca de 'descarregado da Internet'..."
xattr -dr com.apple.quarantine "$DIR" 2>/dev/null || true

mkdir -p "$VST3_DEST" "$AU_DEST" "$APP_DEST"

# Fecha o motor se estiver aberto
pkill -x ReelsEngine 2>/dev/null || true
sleep 1

echo "A copiar o VST3 para $VST3_DEST"
rm -rf "$VST3_DEST/808 Reels Capture.vst3"
cp -R "$DIR/808 Reels Capture.vst3" "$VST3_DEST/"

echo "A copiar o AU para $AU_DEST"
rm -rf "$AU_DEST/808 Reels Capture.component"
cp -R "$DIR/808 Reels Capture.component" "$AU_DEST/"

echo "A copiar o motor para $APP_DEST"
rm -rf "$APP_DEST/808 Reels Engine.app"
cp -R "$DIR/808 Reels Engine.app" "$APP_DEST/"

# Cada versão nova tem uma assinatura diferente: a permissão antiga de gravação
# de ecrã deixa de valer. Limpa-a para o macOS voltar a perguntar.
tccutil reset ScreenCapture com.reels808.engine >/dev/null 2>&1 || true
tccutil reset Camera com.reels808.engine >/dev/null 2>&1 || true

# Obriga o macOS a voltar a ler a lista de Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "Instalado com sucesso."
echo "Abra o FL Studio e, se for a primeira vez: Options > Manage plugins > Find more plugins."
echo "Ao abrir o plugin, o macOS vai pedir autorização de Gravação de ecrã e de Câmara."
