param([string]$RomPath='game.nes',[string]$BuildDir='build',[switch]$Verify)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    & ./prepare-deps.ps1
    $cmake=(Get-Command cmake -ErrorAction Stop).Source
    if (-not (Test-Path -LiteralPath $RomPath)) { throw "ROM ausente: $RomPath" }
    $rom=(Resolve-Path -LiteralPath $RomPath).Path
    # The goodNES and No-Intro/Rev 1 dumps have identical PRG/CHR payloads and
    # differ only in the 16-byte header. Hash the payloads, not the whole file.
    $bytes=[System.IO.File]::ReadAllBytes($rom)
    if ($bytes.Length -lt 16 -or $bytes[0] -ne 0x4E -or $bytes[1] -ne 0x45 -or $bytes[2] -ne 0x53 -or $bytes[3] -ne 0x1A) {
        throw 'ROM inválida: cabeçalho iNES ausente.'
    }
    $flags6=[int]$bytes[6]; $flags7=[int]$bytes[7]; $flags8=[int]$bytes[8]; $flags9=[int]$bytes[9]
    $nes2=(($flags7 -band 0x0C) -eq 0x08)
    if ($nes2 -and (($flags9 -band 0x03) -ne 0)) { throw 'ROM NES 2.0 com console não suportado.' }
    $mapper=($flags6 -shr 4) -bor ($flags7 -band 0xF0)
    if ($nes2) { $mapper=$mapper -bor (($flags8 -band 0x0F) -shl 8) }
    if ($mapper -ne 4) { throw "Mapper $mapper não suportado (esperado 4, MMC3)." }
    $trainer=0
    if (($flags6 -band 0x04) -ne 0) { $trainer=512 }
    $prgOffset=16+$trainer
    $prgSize=[int]$bytes[4]*16384
    $chrSize=[int]$bytes[5]*8192
    if ($prgSize -le 0 -or $bytes.Length -ne ($prgOffset+$prgSize+$chrSize)) {
        throw 'ROM inválida: tamanho não corresponde ao cabeçalho.'
    }
    $sha=[System.Security.Cryptography.SHA256]::Create()
    function Get-PayloadHash([int]$Offset,[int]$Length) {
        $digest=$sha.ComputeHash($bytes,$Offset,$Length)
        return ([System.BitConverter]::ToString($digest) -replace '-','').ToLowerInvariant()
    }
    $prgHash=Get-PayloadHash $prgOffset $prgSize
    $chrHash=Get-PayloadHash ($prgOffset+$prgSize) $chrSize
    $sha.Dispose()
    if ($prgHash -ne 'f4130ba655229d9999fa3b2c59984165810c9b9b125c43b1ff899872e7fedff4' -or
        $chrHash -ne '9a4b0b44031afd7d9574b23e4ef24daf1c06a140cfd19ad2db99bd6344780656') {
        throw 'ROM não é a versão USA suportada (PRG/CHR não conferem).'
    }
    New-Item -ItemType Directory -Force $BuildDir | Out-Null
    function Run-Native([string]$Exe,[string]$NativeArguments,[string]$Log) {
        & ./tools/run-hidden.ps1 -Executable $Exe -Arguments $NativeArguments -Log $Log
        if ($LASTEXITCODE -ne 0) { throw "Falha: veja $Log.log e $Log.err.log" }
    }
    Run-Native $cmake "-S . -B `"$BuildDir`" -G `"MinGW Makefiles`" -DCMAKE_BUILD_TYPE=Release -DSMB3_ROM=`"$rom`"" "$BuildDir/configure"
    Run-Native $cmake "--build `"$BuildDir`" -j 8" "$BuildDir/compile"
    if ($Verify) {
        $verifierPath=(Resolve-Path "$BuildDir/smb3-verify.exe").Path
        Run-Native $verifierPath "`"$rom`"" "$BuildDir/verify"
    }
    Write-Host "Build: $BuildDir/MushroomRecompiled.exe"
} finally { Pop-Location }
