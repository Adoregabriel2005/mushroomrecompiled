param([string]$Version='1.5.0')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 if($Version -notmatch '^\d+\.\d+(\.\d+)?$'){throw 'Versão inválida'}
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
 Copy-Item docs/INTERFACE.md,docs/BRANDING.md,docs/TESTAR.md,docs/STATUS.md,docs/WIDESCREEN.md "$stage/docs/"
 @"
Mushroom Recompiled v$Version — experimental

Extraia o ZIP inteiro e abra MushroomRecompiled.exe.
Na primeira abertura, escolha sua ROM compatível (USA Rev 1), arquivo .nes,
na lista ou arraste o arquivo para a janela.
A ROM não acompanha o pacote.
Após escolher uma ROM válida, o jogo inicia diretamente, inclusive nas próximas aberturas.
Esc pausa e abre o menu para salvar, carregar, continuar, configurar ou sair.

Widescreen: Configurações > Vídeo > Proporção > 16:9.
O cenário horizontal é ampliado; o HUD original fica no centro com barras pretas.
A ativação e o descarte dos objetos acompanham a área ampliada. Salas verticais e outras cenas ficam no enquadramento nativo. Consulte docs/WIDESCREEN.md.

Setas: mover; Z: correr/cauda; X: pular; Shift: Select; Enter: Start.
Esc: menu do PC; F11: tela cheia. Teclado e gamepad são remapeáveis.

A versão é experimental. Consulte docs/TESTAR.md para relatar problemas.
"@ | Set-Content -Encoding utf8 "$stage/LEIA-ME.txt"
 $forbidden=Get-ChildItem $stage -Recurse -File | Where-Object {$_.Extension -in '.nes','.sav','.save' -or $_.Name -eq 'config.ini'}
 if($forbidden){throw 'Conteúdo privado inesperado no pacote'}
 $zip="releases/$name.zip"
 Compress-Archive -Path $stage -DestinationPath $zip -Force
 $hash=(Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
 "$hash  $name.zip" | Set-Content -Encoding ascii "releases/SHA256SUMS-v$Version.txt"
 $local="releases/$name"
 if(-not(Test-Path -LiteralPath $local)){Copy-Item $stage $local -Recurse}
 Write-Host "Pacote: $zip"
 Write-Host "SHA256: $hash"
}finally{Pop-Location}
