param([string]$RomPath='game.nes',[string]$BuildDir='build',[switch]$Verify)
$ErrorActionPreference='Stop'
Push-Location $PSScriptRoot
try {
    & ./prepare-deps.ps1
    $cmake=(Get-Command cmake -ErrorAction Stop).Source
    if (-not (Test-Path -LiteralPath $RomPath)) { throw "ROM ausente: $RomPath" }
    $rom=(Resolve-Path -LiteralPath $RomPath).Path
    if ((Get-FileHash -LiteralPath $rom -Algorithm SHA256).Hash -ne '4377A7F5E6EB50BDD2AC6F249BF1A7085500ACA8EB41F38545C3A2731C51A579') { throw 'Esperada ROM USA Rev 1 utilizada pelo projeto.' }
    New-Item -ItemType Directory -Force $BuildDir | Out-Null
    function Run-Native([string]$Exe,[string]$NativeArguments,[string]$Log) {
        & ./tools/run-hidden.ps1 -Executable $Exe -Arguments $NativeArguments -Log $Log
        if ($LASTEXITCODE -ne 0) { throw "Falha: veja $Log.log e $Log.err.log" }
    }
    Run-Native $cmake "-S tools/nesrecomp-src/recompiler -B `"$BuildDir/recompiler`" -G `"MinGW Makefiles`" -DCMAKE_BUILD_TYPE=Release" "$BuildDir/recompiler-configure"
    Run-Native $cmake "--build `"$BuildDir/recompiler`" -j 8" "$BuildDir/recompiler-build"
    $compiler=(Resolve-Path "$BuildDir/recompiler/NESRecomp.exe").Path
    Run-Native $compiler "`"$rom`" --game game.toml --cycle-seed-file docs/smb3-cycle-seeds.txt" "$BuildDir/generate"
    Run-Native $cmake "-S . -B `"$BuildDir`" -G `"MinGW Makefiles`" -DCMAKE_BUILD_TYPE=Release" "$BuildDir/configure"
    Run-Native $cmake "--build `"$BuildDir`" -j 8" "$BuildDir/compile"
    if ($Verify) {
        $verifierPath=(Resolve-Path "$BuildDir/smb3-verify.exe").Path
        Run-Native $verifierPath "`"$rom`"" "$BuildDir/verify"
    }
    Write-Host "Build: $BuildDir/MushroomRecompiled-v1.5.0.exe"
} finally { Pop-Location }
