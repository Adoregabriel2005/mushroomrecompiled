param([string]$Version='1.5.0')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 if($Version -notmatch '^\d+\.\d+(\.\d+)?$'){throw 'Invalid version'}
 $name="MushroomRecompiled-v$Version"
 $stageRoot=Join-Path 'build' ('package-'+[Guid]::NewGuid().ToString('N'))
 $stage=Join-Path $stageRoot $name
 New-Item -ItemType Directory -Force "$stage/assets","$stage/docs","$stage/LICENSES",releases | Out-Null
 Copy-Item "build/MushroomRecompiled.exe" "$stage/MushroomRecompiled.exe"
 Copy-Item build/SDL2.dll "$stage/SDL2.dll"
 Copy-Item LICENSE,LICENSE_SCOPE.md "$stage/"
 Copy-Item assets/gamecontrollerdb.txt "$stage/assets/"
 Copy-Item png.png "$stage/png.png"
 Copy-Item LICENSES/SDL2.txt,LICENSES/SDL_GameControllerDB.txt,LICENSES/emu2413.txt "$stage/LICENSES/"
 Copy-Item tools/nesrecomp-src/LICENSE "$stage/LICENSES/NESRecomp.txt"
 Copy-Item docs/INTERFACE.md,docs/BRANDING.md,docs/TESTING.md,docs/STATUS.md,docs/WIDESCREEN.md "$stage/docs/"
 @"
Mushroom Recompiled v$Version — experimental

Extract the whole ZIP and open MushroomRecompiled.exe.
On first launch, pick your compatible ROM (USA Rev 1), a .nes file,
from the list or drag the file onto the window.
The ROM is not included.
After picking a valid ROM, the game starts directly, including on later launches.
Esc pausa e abre o menu para salvar, carregar, continuar, configurar ou sair.

Widescreen: horizontal scenery is expanded; the original HUD stays centred with black bars. Object activation follows the enlarged area. Vertical rooms and other scenes keep the native frame. See docs/WIDESCREEN.md.

Arrows: move; Z: run/tail; X: jump; Shift: Select; Enter: Start.
Esc: PC menu; F11: fullscreen. Keyboard and gamepad are remappable.

This build is experimental. See docs/TESTING.md to report problems.
"@ | Set-Content -Encoding utf8 "$stage/README.txt"
 $forbidden=Get-ChildItem $stage -Recurse -File | Where-Object {$_.Extension -in '.nes','.sav','.save' -or $_.Name -eq 'config.ini'}
 if($forbidden){throw 'Unexpected private content in the package'}
 $zip="releases/$name.zip"
 Compress-Archive -Path $stage -DestinationPath $zip -Force
 $hash=(Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
 "$hash  $name.zip" | Set-Content -Encoding ascii "releases/SHA256SUMS-v$Version.txt"
 $local="releases/$name"
 if(-not(Test-Path -LiteralPath $local)){Copy-Item $stage $local -Recurse}
 Write-Host "Package: $zip"
 Write-Host "SHA256: $hash"
}finally{Pop-Location}
