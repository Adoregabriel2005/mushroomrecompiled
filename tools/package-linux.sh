#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=${1:-1.6.0}
binary=${2:-"$root/build/linux/app/MushroomRecompiled"}
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid version' >&2; exit 2; }
[[ -f "$binary" ]] || { echo "Missing Linux binary: $binary" >&2; exit 1; }
mkdir -p "$root/build" "$root/releases"
stage=$(mktemp -d "$root/build/package-linux.XXXXXXXX")
name="MushroomRecompiled-v$version-Linux-x86_64"
package="$stage/$name"
mkdir -p "$package/assets" "$package/docs" "$package/LICENSES"
install -m755 "$binary" "$package/MushroomRecompiled"
install -m755 "$root/run-steamdeck.sh" "$package/run-steamdeck.sh"
cp "$root/assets/gamecontrollerdb.txt" "$package/assets/"
cp "$root/README.md" "$root/STEAMDECK.md" "$root/LICENSE" "$root/LICENSE_SCOPE.md" "$root/png.png" "$package/"
cp "$root/docs/LINUX_VALIDATION.md" "$package/docs/"
cp "$root/LICENSES/SDL2.txt" "$root/LICENSES/SDL_GameControllerDB.txt" "$root/LICENSES/emu2413.txt" "$package/LICENSES/"
cp "$root/tools/nesrecomp-src/LICENSE" "$package/LICENSES/NESRecomp.txt"
cat > "$package/LEIA-ME-LINUX.txt" <<EOF
Mushroom Recompiled v$version - Linux x86-64

Extraia o pacote inteiro. Forneça sua própria ROM compatível localmente.
Execute: bash run-steamdeck.sh --rom "/caminho/game.nes" --fps 40
Janela: acrescente --windowed. Proporção: --aspect 4:3 ou --aspect 16:9.

Dependências: glibc e SDL2 do sistema. O release v1.6.0 foi compilado
em Ubuntu 22.04 (glibc 2.35, SDL2 2.0.20). Em Debian/Ubuntu:
sudo apt-get install libsdl2-2.0-0
Não altere o sistema somente leitura do SteamOS para instalar dependências.
Se houver incompatibilidade, use um ambiente adequado ou recompile na plataforma.

Steam Deck: adicione MushroomRecompiled como jogo não Steam, selecione o modelo
Gamepad do Steam Input e configure as opções de inicialização:
--rom "/caminho/game.nes" --fullscreen --fps 40

D-pad/stick: mover; A/Y: pular; B/X: correr; Start: pausa NES; L3: Select.
Back/Esc: pausa PC; R1/F5: salvar; L1/F9: carregar; F10: proporção;
F11: tela cheia; Ctrl+Q: sair. Há um único slot de salvamento rápido.
Logs opcionais: MUSHROOM_PERF_LOG=1 bash run-steamdeck.sh --rom "/caminho/game.nes"

O frontend Linux é mínimo. Não inclui o menu completo de configurações Windows.
Compatibilidade física com Steam Deck/SteamOS e jogo completo não foi confirmada.
Veja STEAMDECK.md e docs/LINUX_VALIDATION.md para os testes e limites medidos.
A ROM não acompanha o pacote. Veja os termos de uso em LICENSE_SCOPE.md e LICENSES.
EOF
if find "$package" -type f \( -iname '*.nes' -o -iname '*.sav' -o -iname '*.save' -o -iname '*.c' -o -name config.ini \) -print -quit | grep -q .; then
    echo 'Private input or generated source found in package' >&2; exit 1
fi
tar -czf "$root/releases/$name.tar.gz" -C "$stage" "$name"
echo "Created $root/releases/$name.tar.gz"
