param([string]$Version='1.3')
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
 if($Version -notmatch '^\d+\.\d+(\.\d+)?$'){throw 'Versão inválida'}
 $name="SMB3-Recomp-v$Version"
 $stageRoot=Join-Path 'build' ('package-'+[Guid]::NewGuid().ToString('N'))
 $stage=Join-Path $stageRoot $name
 New-Item -ItemType Directory -Force "$stage/assets","$stage/docs","$stage/LICENSES",releases | Out-Null
 Copy-Item "build/SuperMarioBros3Recomp-v$Version.exe" "$stage/SuperMarioBros3Recomp.exe"
 Copy-Item build/SDL2.dll "$stage/SDL2.dll"
 Copy-Item LICENSE,LICENSE_SCOPE.md "$stage/"
 Copy-Item assets/official-logo.png,assets/official-key-art.png,assets/official-cover.png,assets/gamecontrollerdb.txt "$stage/assets/"
 Copy-Item LICENSES/SDL2.txt,LICENSES/SDL_GameControllerDB.txt,LICENSES/emu2413.txt "$stage/LICENSES/"
 Copy-Item tools/nesrecomp-src/LICENSE "$stage/LICENSES/NESRecomp.txt"
 Copy-Item docs/INTERFACE.md,docs/ARTES.md,docs/TESTAR.md,docs/STATUS.md,docs/WIDESCREEN.md "$stage/docs/"
 @"
Super Mario Bros. 3 Recompiled v$Version — experimental

Extraia o ZIP inteiro e abra SuperMarioBros3Recomp.exe.
Selecione sua ROM Super Mario Bros. 3 (USA) (Rev 1), arquivo .nes.
A ROM não acompanha o pacote.

Widescreen: Configurações > Vídeo > Proporção > 16:9.
O cenário horizontal é ampliado; o HUD original fica no centro com barras pretas.
A ativação de inimigos continua original. Salas verticais e outras cenas ficam no enquadramento nativo. Consulte docs/WIDESCREEN.md.

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
