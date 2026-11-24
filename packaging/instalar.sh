#!/bin/bash
# Instala o C00KUP (808College): plugin (VST3 + AU) e motor de vídeo.
# Também remove a versão antiga "808 Reels Capture", se existir (os vídeos não são tocados).
# Uso (no Terminal):  bash ~/Downloads/C00KUP/instalar.sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"
APP_DEST="$HOME/Applications"

echo "A remover a marca de 'descarregado da Internet'..."
xattr -dr com.apple.quarantine "$DIR" 2>/dev/null || true

mkdir -p "$VST3_DEST" "$AU_DEST" "$APP_DEST"

# Fecha os motores (novo e antigo) se estiverem abertos
pkill -x C00KUPEngine 2>/dev/null || true
pkill -x ReelsEngine 2>/dev/null || true
sleep 1

# --- Versão antiga "808 Reels Capture" -------------------------------------
if [ -e "$VST3_DEST/808 Reels Capture.vst3" ] || [ -e "$AU_DEST/808 Reels Capture.component" ] || [ -e "$APP_DEST/808 Reels Engine.app" ]; then
    echo "A remover a versão antiga \"808 Reels Capture\"..."
fi
rm -rf "$VST3_DEST/808 Reels Capture.vst3" "$AU_DEST/808 Reels Capture.component" "$APP_DEST/808 Reels Engine.app"
tccutil reset ScreenCapture com.reels808.engine >/dev/null 2>&1 || true
tccutil reset Camera com.reels808.engine >/dev/null 2>&1 || true

# --- C00KUP -------------------------------------------------------------------
echo "A copiar o VST3 para $VST3_DEST"
rm -rf "$VST3_DEST/C00KUP.vst3"
cp -R "$DIR/C00KUP.vst3" "$VST3_DEST/"

echo "A copiar o AU para $AU_DEST"
rm -rf "$AU_DEST/C00KUP.component"
cp -R "$DIR/C00KUP.component" "$AU_DEST/"

echo "A copiar o motor para $APP_DEST"
rm -rf "$APP_DEST/C00KUP Engine.app"
cp -R "$DIR/C00KUP Engine.app" "$APP_DEST/"

# Cada versão nova tem uma assinatura diferente: a permissão antiga de gravação
# de ecrã e câmara deixa de valer. Limpa-a para o macOS voltar a perguntar.
tccutil reset ScreenCapture com.808college.c00kup.engine >/dev/null 2>&1 || true
tccutil reset Camera com.808college.c00kup.engine >/dev/null 2>&1 || true

# Obriga o macOS a voltar a ler a lista de Audio Units
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo ""
echo "C00KUP instalado com sucesso."
echo "No FL Studio: Options > Manage plugins > Find more plugins."
echo "Ao abrir o plugin, o macOS vai pedir autorização de Gravação de ecrã e de Câmara."
